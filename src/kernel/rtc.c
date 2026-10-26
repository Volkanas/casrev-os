#include "rtc.h"
#include "io.h"
#include "types.h"

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static void rtc_wait_ready(void)
{
    /* Guncelleme suresi dolana kadar bekle (en fazla ~1 milyon deneme). */
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((cmos_read(0x0A) & 0x80u) == 0) {
            return;
        }
    }
}

static uint8_t bcd_to_bin(uint8_t value)
{
    return (uint8_t)((value & 0x0Fu) + ((value >> 4) * 10u));
}

void rtc_read(rtc_time_t *time)
{
    if (!time) {
        return;
    }

    rtc_wait_ready();

    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour   = cmos_read(0x04);
    uint8_t day    = cmos_read(0x07);
    uint8_t month  = cmos_read(0x08);
    uint8_t year   = cmos_read(0x09);
    uint8_t status = cmos_read(0x0B);

    int bcd = (status & 0x04u) == 0;

    if (bcd) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        day    = bcd_to_bin(day);
        month  = bcd_to_bin(month);
        year   = bcd_to_bin(year);
    }

    uint8_t pm = (uint8_t)(hour & 0x80u);
    hour &= 0x7Fu;
    if (bcd) {
        hour = bcd_to_bin(hour);
    }

    if ((status & 0x02u) == 0) {
        /* 12 saatlik mod: 12 -> 0, PM ise +12. */
        if (hour == 12) {
            hour = 0;
        }
        if (pm) {
            hour = (uint8_t)(hour + 12u);
        }
    }

    time->second = second;
    time->minute = minute;
    time->hour   = hour;
    time->day    = day;
    time->month  = month;
    time->year   = (uint16_t)(2000u + year);
}

void rtc_format_time(char *out)
{
    rtc_time_t t;
    rtc_read(&t);

    uint8_t h = t.hour;
    uint8_t m = t.minute;
    uint8_t s = t.second;

    if (h > 23) h = 0;
    if (m > 59) m = 0;
    if (s > 59) s = 0;

    out[0] = (char)('0' + h / 10);
    out[1] = (char)('0' + h % 10);
    out[2] = ':';
    out[3] = (char)('0' + m / 10);
    out[4] = (char)('0' + m % 10);
    out[5] = ':';
    out[6] = (char)('0' + s / 10);
    out[7] = (char)('0' + s % 10);
    out[8] = '\0';
}

void rtc_format_date(char *out)
{
    rtc_time_t t;
    rtc_read(&t);

    uint8_t d = t.day;
    uint8_t m = t.month;
    uint16_t y = t.year;

    if (d < 1 || d > 31) d = 1;
    if (m < 1 || m > 12) m = 1;
    if (y < 2000) y = 2000;

    out[0] = (char)('0' + d / 10);
    out[1] = (char)('0' + d % 10);
    out[2] = '.';
    out[3] = (char)('0' + m / 10);
    out[4] = (char)('0' + m % 10);
    out[5] = '.';
    out[6] = (char)('0' + (y / 1000) % 10);
    out[7] = (char)('0' + (y / 100) % 10);
    out[8] = (char)('0' + (y / 10) % 10);
    out[9] = (char)('0' + y % 10);
    out[10] = '\0';
}
