#include "vga.h"
#include "types.h"
#include "ui.h"

/*
 * Eski VGA metin surucusu yerine grafik arayuzun terminal katmani.
 * API ayni kaldi; boylece console.c ve kabuk degismeden calisiyor.
 */

void vga_init(void)
{
    ui_init();
}

void vga_clear(void)
{
    ui_term_clear();
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    ui_term_set_color(fg, bg);
}

void vga_putc(char c)
{
    ui_term_putc(c);
}

void vga_write(const char *str)
{
    while (*str) {
        ui_term_putc(*str++);
    }
}
