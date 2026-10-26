#ifndef CASREV_TIMER_H
#define CASREV_TIMER_H

#include "types.h"

#define TIMER_HZ 100

void timer_init(void);
void timer_handler(void);
uint32_t timer_ticks(void);
uint32_t timer_seconds(void);

#endif
