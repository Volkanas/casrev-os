#include "console.h"
#include "serial.h"
#include "types.h"
#include "vga.h"

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

void console_putc(char c)
{
    vga_putc(c);
    if (c == '\n') {
        serial_putc('\r');
    }
    serial_putc(c);
}

void console_write(const char *str)
{
    while (*str) {
        console_putc(*str++);
    }
}

static void print_uint(uint32_t value, uint32_t base, int uppercase)
{
    static const char lower[] = "0123456789abcdef";
    static const char upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? upper : lower;
    char buf[32];
    int i = 0;

    if (value == 0) {
        console_putc('0');
        return;
    }

    while (value > 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    while (i > 0) {
        console_putc(buf[--i]);
    }
}

static void print_int(int32_t value)
{
    if (value < 0) {
        console_putc('-');
        print_uint(0u - (uint32_t)value, 10, 0);
    } else {
        print_uint((uint32_t)value, 10, 0);
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            console_putc(*fmt);
            continue;
        }

        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            console_write(s ? s : "(null)");
            break;
        }
        case 'c':
            console_putc((char)va_arg(ap, int));
            break;
        case 'd':
        case 'i':
            print_int((int32_t)va_arg(ap, int));
            break;
        case 'u':
            print_uint((uint32_t)va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':
            print_uint((uint32_t)va_arg(ap, unsigned int), 16, 0);
            break;
        case 'X':
            print_uint((uint32_t)va_arg(ap, unsigned int), 16, 1);
            break;
        case 'p':
            console_write("0x");
            print_uint((uint32_t)(uintptr_t)va_arg(ap, void *), 16, 0);
            break;
        case '%':
            console_putc('%');
            break;
        case '\0':
            va_end(ap);
            return;
        default:
            console_putc('%');
            console_putc(*fmt);
            break;
        }
    }

    va_end(ap);
}
