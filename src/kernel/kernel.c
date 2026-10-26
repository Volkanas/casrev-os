#include "bootinfo.h"
#include "console.h"
#include "heap.h"
#include "idt.h"
#include "keyboard.h"
#include "mouse.h"
#include "paging.h"
#include "pmm.h"
#include "rtc.h"
#include "serial.h"
#include "shell.h"
#include "string.h"
#include "timer.h"
#include "types.h"
#include "ui.h"
#include "vfs.h"
#include "vga.h"

void kernel_main(void)
{
    /* 3. Kernel girisinde ekran bellegini (Framebuffer) 0x00 ile doldurarak temizle */
    const boot_info_t *bi = boot_info_get();
    if (bi->magic == BOOTINFO_MAGIC && bi->fb_addr != 0) {
        uint32_t pitch = bi->pitch ? bi->pitch : 1600;
        uint32_t height = bi->height ? bi->height : 600;
        uint32_t fb_size = pitch * height;
        if (fb_size < 800u * 600u * 2u) {
            fb_size = 800u * 600u * 2u;
        }
        memset((void *)bi->fb_addr, 0x00, fb_size);
    }

    vga_init();
    serial_init();

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("casrev-os v0.9\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("========================================\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("Cekirdek yuklendi, bellek yonetimi kuruluyor...\n\n");

    pmm_init();
    paging_init(pmm_highest_address());
    heap_init();
    vfs_init();

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("  Sayfalama : %s, %u MB kimlik esleme (test %s)\n",
            paging_enabled() ? "ACIK" : "KAPALI",
            paging_mapped_bytes() / (1024 * 1024),
            paging_self_test() ? "OK" : "HATA");
    kprintf("  Heap      : %u KB, test %s\n",
            heap_total_bytes() / 1024,
            heap_self_test() ? "OK" : "BASARISIZ");
    ui_poll();

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("\nKesmeler ve cevre birimleri kuruluyor...\n\n");

    idt_init();
    timer_init();
    keyboard_init();
    mouse_init();

    char clock[9];
    rtc_format_time(clock);

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("  IDT    : 256 giris, istisnalar + IRQ 0-15\n");
    kprintf("  PIC    : IRQ0-7 -> 0x20, IRQ8-15 -> 0x28\n");
    kprintf("  PIT    : %u Hz zamanlayici\n", TIMER_HZ);
    kprintf("  Klavye : PS/2, IRQ1\n");
    kprintf("  Fare   : PS/2 aux, IRQ12\n");
    kprintf("  RTC    : %s\n", clock);
    kprintf("  Grafik : %ux%ux%u, pitch %u, FB 0x%x\n\n",
            (uint32_t)bi->width, (uint32_t)bi->height,
            (uint32_t)bi->bpp, (uint32_t)bi->pitch, bi->fb_addr);
    ui_poll();

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("Klavyeden yazin. Komut listesi icin: help\n\n");
    ui_poll();

    interrupts_enable();

    shell_init();
    shell_run();
}
