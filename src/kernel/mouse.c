#include "mouse.h"
#include "io.h"
#include "types.h"

#define MOUSE_MAX_X 791     /* 800 - 9 (imlec genisligi) */
#define MOUSE_MAX_Y 587     /* 600 - 13 (imlec yuksekligi) */

static volatile int     mouse_x = 400;
static volatile int     mouse_y = 300;
static volatile uint8_t mouse_buttons;

static uint8_t packet[3];
static int     packet_index;

static int mouse_wait_write(void)
{
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((inb(0x64) & 0x02u) == 0) {
            return 1;
        }
    }
    return 0;
}

static int mouse_wait_read(void)
{
    for (uint32_t i = 0; i < 100000u; i++) {
        if (inb(0x64) & 0x01u) {
            return 1;
        }
    }
    return 0;
}

static uint8_t mouse_read(void)
{
    if (!mouse_wait_read()) {
        return 0;
    }
    return inb(0x60);
}

static void mouse_write(uint8_t value)
{
    if (!mouse_wait_write()) {
        return;
    }
    outb(0x64, 0xD4);               /* sonraki bayt aux cihazina */
    if (!mouse_wait_write()) {
        return;
    }
    outb(0x60, value);
}

void mouse_init(void)
{
    packet_index = 0;
    mouse_buttons = 0;

    /* Aux portunu ac. */
    if (mouse_wait_write()) {
        outb(0x64, 0xA8);
    }

    /* 8042 komut baytini oku; IRQ12'yi ac, aux saatini kapatma. */
    if (mouse_wait_write()) {
        outb(0x64, 0x20);
    }

    uint8_t status = mouse_read();
    status |= 0x02u;                /* IRQ12 acik */
    status &= (uint8_t)~0x20u;      /* aux clock acik */

    if (mouse_wait_write()) {
        outb(0x64, 0x60);
    }
    if (mouse_wait_write()) {
        outb(0x60, status);
    }

    /* Varsayilan ayarlar ve veri raporlamayi ac. */
    mouse_write(0xF6);
    (void)mouse_read();
    mouse_write(0xF4);
    (void)mouse_read();

    /* Varsa artik baytlari temizle. */
    while (inb(0x64) & 0x01u) {
        (void)inb(0x60);
    }

    /* IRQ2 (kaskad) ve IRQ12 (aux) maskesini kaldir. */
    outb(0x21, (uint8_t)(inb(0x21) & ~0x04u));
    outb(0xA1, (uint8_t)(inb(0xA1) & ~0x10u));

    packet_index = 0;
}

void mouse_handler(void)
{
    /* 8042 status biti 5: veri AUX (fare) portundan gelmeli. */
    if ((inb(0x64) & 0x20u) == 0) {
        return;
    }

    uint8_t data = inb(0x60);

    if (packet_index == 0 && (data & 0x08u) == 0) {
        return;
    }

    packet[packet_index++] = data;

    if (packet_index < 3) {
        return;
    }
    packet_index = 0;

    if (packet[0] & 0xC0u) {        /* X/Y overflow */
        return;
    }

    int dx = (int)packet[1] - ((packet[0] & 0x10u) ? 256 : 0);
    int dy = (int)packet[2] - ((packet[0] & 0x20u) ? 256 : 0);

    int x = mouse_x + dx;
    int y = mouse_y - dy;

    if (x < 0) x = 0;
    if (x > MOUSE_MAX_X) x = MOUSE_MAX_X;
    if (y < 0) y = 0;
    if (y > MOUSE_MAX_Y) y = MOUSE_MAX_Y;

    mouse_x = x;
    mouse_y = y;
    mouse_buttons = (uint8_t)(packet[0] & 0x07u);
}

void mouse_get_state(int *x, int *y, int *buttons)
{
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}
