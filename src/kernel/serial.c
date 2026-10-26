#include "serial.h"
#include "io.h"

#define COM1 0x3F8

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
