#ifndef CASREV_GFX_H
#define CASREV_GFX_H

#include "types.h"

/* Standart VGA Renkleri (0-15) */
#define GFX_BLACK          0
#define GFX_BLUE           1
#define GFX_GREEN          2
#define GFX_CYAN           3
#define GFX_RED            4
#define GFX_MAGENTA        5
#define GFX_BROWN          6
#define GFX_LIGHT_GREY     7
#define GFX_DARK_GREY      8
#define GFX_LIGHT_BLUE     9
#define GFX_LIGHT_GREEN    10
#define GFX_LIGHT_CYAN     11
#define GFX_LIGHT_RED      12
#define GFX_LIGHT_MAGENTA  13
#define GFX_YELLOW         14
#define GFX_WHITE          15

/* Modern Masaustu UI Renkleri (Palet 16-31) */
enum ui_color {
    UI_DESKTOP        = 16, /* Derin lacivert/antrasit masaustu */
    UI_TASKBAR        = 17, /* Koyu akrilik gorev cubugu */
    UI_WINDOW         = 18, /* Pencere ici koyu arka plan */
    UI_TITLE_ACTIVE   = 19, /* Aktif pencere basligi koyu mavi */
    UI_TITLE_INACT    = 20, /* Pasif pencere basligi */
    UI_BORDER         = 21, /* Pencere kenarligi */
    UI_ACCENT         = 22, /* Modern vurgu mavisi */
    UI_ACCENT_HOVER   = 23, /* Vurgu mavisi hover */
    UI_CLOSE_RED      = 24, /* Kapat butonu kirmizi */
    UI_CLOSE_HOVER    = 25, /* Kapat hover kirmizi */
    UI_MINIMIZE       = 26, /* Kucult butonu sari */
    UI_MENU_BG        = 27, /* Baslat menusu koyu kart */
    UI_MENU_HOVER     = 28, /* Menu ogesi hover */
    UI_CARD_BG        = 29, /* Kart/Widget kutusu */
    UI_PROGRESS       = 30, /* Canli zümrüt yesili ilerleme */
    UI_DARK_SHADOW    = 31  /* Koyu golge */
};

/* 16-bit RGB565 renk yardimcilari (0..255 kanallar) */
#define GFX_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | \
                (((uint16_t)(g) & 0xFCu) << 3) | \
                (((uint16_t)(b) & 0xF8u) >> 3)))

int  gfx_init(uint32_t fb_addr, uint16_t width, uint16_t height,
              uint16_t pitch, uint8_t bpp, uint32_t font_addr);
int  gfx_ready(void);
void gfx_set_palette(void);
void gfx_flush(void);

void gfx_clear(uint8_t color);
void gfx_pixel(int x, int y, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_fill_circle(int cx, int cy, int r, uint8_t color);

/* Modern UI Cizim Yardimcilari */
void gfx_fill_rounded_rect(int x, int y, int w, int h, int r, uint8_t color);
void gfx_rect_rounded(int x, int y, int w, int h, int r, uint8_t color);
void gfx_shadow(int x, int y, int w, int h, uint8_t color);

/* 16-bit tam renk cizimleri */
uint16_t gfx_rgb(int r, int g, int b);
uint16_t gfx_blend(uint16_t dst, uint16_t src, uint8_t alpha);

void gfx_pixel_rgb(int x, int y, uint16_t color);
void gfx_fill_rect_rgb(int x, int y, int w, int h, uint16_t color);
void gfx_hline_rgb(int x, int y, int w, uint16_t color);
void gfx_vline_rgb(int x, int y, int h, uint16_t color);
void gfx_rect_rgb(int x, int y, int w, int h, uint16_t color);
void gfx_fill_circle_rgb(int cx, int cy, int r, uint16_t color);
void gfx_fill_circle_blend(int cx, int cy, int r, uint16_t color, uint8_t alpha);
void gfx_fill_rounded_rect_rgb(int x, int y, int w, int h, int r, uint16_t color);
void gfx_rect_rounded_rgb(int x, int y, int w, int h, int r, uint16_t color);
void gfx_fill_rounded_rect_gradient_v(int x, int y, int w, int h, int r,
                                      uint16_t top, uint16_t bottom);
void gfx_gradient_v(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void gfx_blend_rect(int x, int y, int w, int h, uint16_t color, uint8_t alpha);
void gfx_shadow_soft(int x, int y, int w, int h);

/* bg == 0xFF ise arka plan cizilmez (saydam metin). */
void gfx_char(int x, int y, unsigned char c, uint8_t fg, uint8_t bg);
void gfx_text(int x, int y, const char *text, uint8_t fg, uint8_t bg);

int gfx_width(void);
int gfx_height(void);

#endif
