#include "string.h"

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = (uint8_t)value;
    }
    return dst;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *orig = dst;
    while ((*dst++ = *src++)) {}
    return orig;
}

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int string_format(char *buf, size_t size, const char *fmt, ...)
{
    if (!buf || size == 0) return 0;

    va_list ap;
    va_start(ap, fmt);

    size_t out_idx = 0;

    for (; *fmt && out_idx + 1 < size; fmt++) {
        if (*fmt != '%') {
            buf[out_idx++] = *fmt;
            continue;
        }

        fmt++;

        int pad_zero = 0;
        int width = 0;
        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && out_idx + 1 < size) {
                buf[out_idx++] = *s++;
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            buf[out_idx++] = c;
            break;
        }
        case 'd':
        case 'i': {
            int32_t val = (int32_t)va_arg(ap, int);
            uint32_t uval;
            if (val < 0) {
                if (out_idx + 1 < size) buf[out_idx++] = '-';
                uval = 0u - (uint32_t)val;
            } else {
                uval = (uint32_t)val;
            }

            char num_buf[16];
            int ni = 0;
            if (uval == 0) {
                num_buf[ni++] = '0';
            } else {
                while (uval > 0) {
                    num_buf[ni++] = (char)('0' + (uval % 10));
                    uval /= 10;
                }
            }
            if (pad_zero) {
                while (ni < width && ni < 15) {
                    num_buf[ni++] = '0';
                }
            }
            while (ni > 0 && out_idx + 1 < size) {
                buf[out_idx++] = num_buf[--ni];
            }
            break;
        }
        case 'u': {
            uint32_t uval = va_arg(ap, uint32_t);
            char num_buf[16];
            int ni = 0;
            if (uval == 0) {
                num_buf[ni++] = '0';
            } else {
                while (uval > 0) {
                    num_buf[ni++] = (char)('0' + (uval % 10));
                    uval /= 10;
                }
            }
            if (pad_zero) {
                while (ni < width && ni < 15) {
                    num_buf[ni++] = '0';
                }
            }
            while (ni > 0 && out_idx + 1 < size) {
                buf[out_idx++] = num_buf[--ni];
            }
            break;
        }
        case '%': {
            buf[out_idx++] = '%';
            break;
        }
        default:
            buf[out_idx++] = *fmt;
            break;
        }
    }

    va_end(ap);
    buf[out_idx] = '\0';
    return (int)out_idx;
}
