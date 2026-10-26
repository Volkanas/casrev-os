#ifndef CASREV_RTC_H
#define CASREV_RTC_H

#include "types.h"

typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_t;

void rtc_read(rtc_time_t *time);
void rtc_format_time(char *out);      /* "HH:MM:SS"  (9 bayt) */
void rtc_format_date(char *out);      /* "DD.MM.YYYY" (11 bayt) */

#endif
