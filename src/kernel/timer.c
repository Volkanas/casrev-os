#include "timer.h"
#include "io.h"

#define PIT_FREQUENCY 1193182

static volatile uint32_t ticks;

void timer_init(void)
{
    uint32_t divisor = PIT_FREQUENCY / TIMER_HZ;

    outb(0x43, 0x36);                       /* kanal 0, lobyte/hibyte, kare dalga */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    ticks = 0;
}

void timer_handler(void)
{
    ticks++;
}

uint32_t timer_ticks(void)
{
    return ticks;
}

uint32_t timer_seconds(void)
{
    return ticks / TIMER_HZ;
}
