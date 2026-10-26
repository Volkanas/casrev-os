#include "idt.h"
#include "console.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "types.h"
#include "ui.h"
#include "vga.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

/* interrupts.asm'deki yigin duzeniyle birebir ayni olmali. */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t vector, error, eip, cs, eflags;
} registers_t;

extern uint32_t isr_stub_table[48];

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idtr;

static const char *exception_names[32] = {
    "Sifira bolme",
    "Debug",
    "NMI",
    "Breakpoint",
    "Tasma (overflow)",
    "Sinir asimi",
    "Gecersiz opcode",
    "Cihaz yok",
    "Cift hata",
    "Koprocessor segment",
    "Gecersiz TSS",
    "Segment yok",
    "Yigin hatasi",
    "Genel koruma hatasi",
    "Sayfa hatasi",
    "Rezerve",
    "x87 kayan nokta",
    "Hizalama hatasi",
    "Makine kontrolu",
    "SIMD kayan nokta",
    "Sanallastirma",
    "Kontrol korumasi",
    "Rezerve",
    "Rezerve",
    "Rezerve",
    "Rezerve",
    "Rezerve",
    "Rezerve",
    "Hipervizor",
    "VMM iletisimi",
    "Guvenlik",
    "Rezerve"
};

static void idt_set_gate(uint8_t vector, uint32_t handler)
{
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector    = 0x08;      /* GDT kod segmenti */
    idt[vector].zero        = 0;
    idt[vector].flags       = 0x8E;      /* present, ring 0, 32-bit interrupt gate */
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

static void pic_remap(void)
{
    /* ICW1: baslat, ICW4 gerekli */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    /* ICW2: vektor ofsetleri */
    outb(0x21, 0x20); io_wait();   /* master IRQ0 -> 0x20 */
    outb(0xA1, 0x28); io_wait();   /* slave  IRQ8 -> 0x28 */

    /* ICW3: kaskad */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();

    /* ICW4: 8086 modu */
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* IRQ0 (PIT), IRQ1 (klavye), IRQ2 (kaskad), IRQ12 (fare) acik */
    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

void idt_init(void)
{
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, 0);
    }

    for (uint32_t i = 0; i < 48; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i]);
    }

    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    pic_remap();
}

void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

static void exception_handler(registers_t *r)
{
    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n\n[ ISTISNA ] %s\n", exception_names[r->vector]);
    kprintf("vektor=%u  hata=%x\n", r->vector, r->error);
    if (r->vector == 14) {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf("CR2=%x (hatali sanal adres)\n", cr2);
    }
    kprintf("EIP=%x  CS=%x  EFLAGS=%x\n", r->eip, r->cs, r->eflags);
    kprintf("Sistem durduruldu.\n");
    ui_render();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void irq_handler(registers_t *r)
{
    uint32_t irq = r->vector - 32;

    switch (irq) {
    case 0:
        timer_handler();
        break;
    case 1:
        keyboard_handler();
        break;
    case 12:
        mouse_handler();
        break;
    default:
        break;
    }

    if (irq >= 8) {
        outb(0xA0, 0x20);           /* slave EOI */
    }
    outb(0x20, 0x20);               /* master EOI */
}

void interrupt_dispatch(registers_t *r)
{
    if (r->vector < 32) {
        exception_handler(r);
    } else if (r->vector < 48) {
        irq_handler(r);
    }
}
