#include "serial.h"
#include "io.h"

#define COM1 0x3F8
#define COM2 0x2F8

static int serial2_available = 0;

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* kesmeleri kapat */
    outb(COM1 + 3, 0x80);   /* DLAB ac */
    outb(COM1 + 0, 0x01);   /* bolucu = 1 -> 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO ac, temizle */
    outb(COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
}

void serial_putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* verici bosalana kadar bekle */
    }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *str)
{
    while (*str) {
        serial_putc(*str++);
    }
}

void serial2_init(void)
{
    /* Scratch register ile COM2'nin gercekten var olup olmadigini anla. */
    outb(COM2 + 7, 0xA5);
    if (inb(COM2 + 7) != 0xA5) {
        serial2_available = 0;
        return;
    }

    outb(COM2 + 1, 0x00);
    outb(COM2 + 3, 0x80);
    outb(COM2 + 0, 0x01);   /* 115200 baud */
    outb(COM2 + 1, 0x00);
    outb(COM2 + 3, 0x03);   /* 8N1 */
    outb(COM2 + 2, 0xC7);   /* FIFO ac, temizle */
    outb(COM2 + 4, 0x0B);   /* DTR, RTS, OUT2 */
    serial2_available = 1;
}

int serial2_present(void)
{
    return serial2_available;
}

void serial2_putc(char c)
{
    if (!serial2_available) return;

    uint32_t spin = 0;
    while ((inb(COM2 + 5) & 0x20) == 0) {
        if (++spin > 200000u) return;
    }
    outb(COM2, (uint8_t)c);
}

void serial2_write(const char *str)
{
    if (!serial2_available || !str) return;
    while (*str) {
        serial2_putc(*str++);
    }
}

int serial2_read(void)
{
    if (!serial2_available) return -1;
    if ((inb(COM2 + 5) & 0x01) == 0) return -1;
    return (int)inb(COM2);
}
