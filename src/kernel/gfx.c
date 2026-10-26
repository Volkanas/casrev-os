#include "gfx.h"
#include "io.h"
#include "string.h"
#include "types.h"

#define GFX_MAX_W 1024
#define GFX_MAX_H 768

/*
 * 16-bit RGB565 arka tampon.
 * Eski 8-bit palet indeksleri legacy_lut[] uzerinden RGB565'e eslenir;
 * yeni tam renkli cizimler dogrudan gfx_*_rgb() fonksiyonlarini kullanir.
 */
static uint16_t backbuffer[GFX_MAX_W * GFX_MAX_H];
static uint8_t *framebuffer;
static uint16_t screen_w;
static uint16_t screen_h;
static uint16_t screen_pitch;
static uint32_t font_addr;
static int      initialized;
static uint16_t legacy_lut[256];

/*
 * Turkce Q klavyesi icin 8x8 ozel glifler.
 * Kodlar ISO-8859-9 / CP1254 ile aynidir.
 */
static const uint8_t tr_font[12][8] = {
    /* 0xC7 - C cedilla */
    { 0x78, 0x84, 0x80, 0x80, 0x84, 0x78, 0x08, 0x10 },
    /* 0xD0 - G breve */
    { 0x50, 0x78, 0x84, 0x80, 0x9C, 0x84, 0x78, 0x00 },
    /* 0xD6 - O diaeresis */
    { 0x50, 0x78, 0x84, 0x84, 0x84, 0x84, 0x78, 0x00 },
    /* 0xDC - U diaeresis */
    { 0x50, 0x84, 0x84, 0x84, 0x84, 0x84, 0x78, 0x00 },
    /* 0xDD - I dot above */
    { 0x20, 0x00, 0x70, 0x20, 0x20, 0x20, 0x70, 0x00 },
    /* 0xDE - S cedilla */
    { 0x78, 0x84, 0x70, 0x08, 0x84, 0x78, 0x08, 0x10 },
    /* 0xE7 - c cedilla */
    { 0x70, 0x88, 0x80, 0x80, 0x88, 0x70, 0x08, 0x10 },
    /* 0xF0 - g breve */
    { 0x50, 0x78, 0x84, 0x84, 0x7C, 0x04, 0x04, 0x78 },
    /* 0xF6 - o diaeresis */
    { 0x50, 0x00, 0x70, 0x88, 0x88, 0x88, 0x70, 0x00 },
    /* 0xFC - u diaeresis */
    { 0x50, 0x00, 0x88, 0x88, 0x88, 0x88, 0x70, 0x00 },
    /* 0xFD - dotless i */
    { 0x00, 0x00, 0x60, 0x20, 0x20, 0x20, 0x70, 0x00 },
    /* 0xFE - s cedilla */
    { 0x00, 0x78, 0x80, 0x70, 0x08, 0xF0, 0x08, 0x10 }
};

static const uint8_t *tr_glyph(unsigned char c)
{
    switch (c) {
    case 0xC7: return tr_font[0];
    case 0xD0: return tr_font[1];
    case 0xD6: return tr_font[2];
    case 0xDC: return tr_font[3];
    case 0xDD: return tr_font[4];
    case 0xDE: return tr_font[5];
    case 0xE7: return tr_font[6];
    case 0xF0: return tr_font[7];
    case 0xF6: return tr_font[8];
    case 0xFC: return tr_font[9];
    case 0xFD: return tr_font[10];
    case 0xFE: return tr_font[11];
    default:   return 0;
    }
}

/*
 * 0-15 : klasik VGA renkleri
 * 16-31: modern masaustu arayuz renkleri
 * 32-247: 6x6x6 RGB renk kupu (legacy uyumluluk)
 * 248-255: 8 kademeli gri tonlar
 *
 * Tablo artik 8-bit kanal degerleri tutar; 16-bit modda dogrudan
 * RGB565'e cevrilir. 8-bit DAC yalnizca gfx_set_palette() icinde kullanilir.
 */
static const uint8_t palette[32][3] = {
    /* 0-15: Standart VGA */
    {   0,   0,   0 }, {   0,   0, 170 }, {   0, 170,   0 }, {   0, 170, 170 },
    { 170,   0,   0 }, { 170,   0, 170 }, { 170,  85,   0 }, { 170, 170, 170 },
    {  85,  85,  85 }, {  85,  85, 255 }, {  85, 255,  85 }, {  85, 255, 255 },
    { 255,  85,  85 }, { 255,  85, 255 }, { 255, 255,  85 }, { 255, 255, 255 },
    /* 16-31: Modern Masaustu UI */
    {  10,  13,  22 }, /* 16: UI_DESKTOP */
    {  13,  17,  27 }, /* 17: UI_TASKBAR */
    {  20,  24,  34 }, /* 18: UI_WINDOW */
    {  16,  58, 102 }, /* 19: UI_TITLE_ACTIVE */
    {  32,  37,  49 }, /* 20: UI_TITLE_INACT */
    {  52,  62,  82 }, /* 21: UI_BORDER */
    {   0, 122, 212 }, /* 22: UI_ACCENT */
    {  38, 166, 245 }, /* 23: UI_ACCENT_HOVER */
    { 190,  52,  58 }, /* 24: UI_CLOSE_RED */
    { 235,  72,  78 }, /* 25: UI_CLOSE_HOVER */
    { 222, 170,  42 }, /* 26: UI_MINIMIZE */
    {  21,  26,  37 }, /* 27: UI_MENU_BG */
    {  40,  50,  70 }, /* 28: UI_MENU_HOVER */
    {  30,  35,  48 }, /* 29: UI_CARD_BG */
    {  32, 190, 120 }, /* 30: UI_PROGRESS */
    {   4,   6,   9 }  /* 31: UI_DARK_SHADOW */
};

static uint16_t pack_rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                      ((uint16_t)(g & 0xFC) << 3) |
                      ((uint16_t)b >> 3));
}

uint16_t gfx_rgb(int r, int g, int b)
{
    return pack_rgb(r, g, b);
}

static void build_legacy_lut(void)
{
    for (int i = 0; i < 32; i++) {
        legacy_lut[i] = pack_rgb(palette[i][0], palette[i][1], palette[i][2]);
    }

    for (int i = 32; i < 248; i++) {
        int idx = i - 32;
        int r = ((idx / 36) % 6) * 255 / 5;
        int g = ((idx / 6) % 6) * 255 / 5;
        int b = (idx % 6) * 255 / 5;
        legacy_lut[i] = pack_rgb(r, g, b);
    }

    for (int i = 248; i < 256; i++) {
        int gray = (i - 248) * 255 / 7;
        legacy_lut[i] = pack_rgb(gray, gray, gray);
    }
}

void gfx_set_palette(void)
{
    outb(0x3C8, 0);
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t r;
        uint8_t g;
        uint8_t b;

        if (i < 32) {
            r = palette[i][0] >> 2;
            g = palette[i][1] >> 2;
            b = palette[i][2] >> 2;
        } else if (i < 248) {
            uint32_t idx = i - 32;
            r = (uint8_t)(((idx / 36) % 6) * 63 / 5);
            g = (uint8_t)(((idx / 6) % 6) * 63 / 5);
            b = (uint8_t)((idx % 6) * 63 / 5);
        } else {
            uint8_t gray = (uint8_t)((i - 248) * 63 / 7);
            r = gray;
            g = gray;
            b = gray;
        }

        outb(0x3C9, r);
        outb(0x3C9, g);
        outb(0x3C9, b);
    }
}

int gfx_init(uint32_t fb, uint16_t width, uint16_t height,
             uint16_t pitch, uint8_t bpp, uint32_t font)
{
    initialized = 0;

    if (bpp != 16 || width == 0 || height == 0 ||
        width > GFX_MAX_W || height > GFX_MAX_H ||
        pitch < (uint16_t)(width * 2) || fb == 0) {
        return 0;
    }

    framebuffer  = (uint8_t *)(uintptr_t)fb;
    screen_w     = width;
    screen_h     = height;
    screen_pitch = pitch;
    font_addr    = font;

    build_legacy_lut();

    /* Framebuffer'i temizle (16-bit: pitch * height bayt) */
    memset(framebuffer, 0x00, (uint32_t)screen_pitch * screen_h);

    initialized = 1;
    gfx_clear(0);
    gfx_flush();
    return 1;
}

int gfx_ready(void)
{
    return initialized;
}

int gfx_width(void)
{
    return (int)screen_w;
}

int gfx_height(void)
{
    return (int)screen_h;
}

void gfx_flush(void)
{
    if (!initialized) {
        return;
    }

    uint32_t row_bytes = (uint32_t)screen_w * sizeof(uint16_t);
    for (uint16_t y = 0; y < screen_h; y++) {
        memcpy(framebuffer + (uint32_t)y * screen_pitch,
               backbuffer + (uint32_t)y * GFX_MAX_W,
               row_bytes);
    }
}

void gfx_clear(uint8_t color)
{
    uint16_t c = legacy_lut[color];

    for (uint16_t y = 0; y < screen_h; y++) {
        uint16_t *row = backbuffer + (uint32_t)y * GFX_MAX_W;
        for (uint16_t x = 0; x < screen_w; x++) {
            row[x] = c;
        }
    }
}

void gfx_pixel_rgb(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) {
        return;
    }
    backbuffer[(uint32_t)y * GFX_MAX_W + (uint32_t)x] = color;
}

void gfx_pixel(int x, int y, uint8_t color)
{
    gfx_pixel_rgb(x, y, legacy_lut[color]);
}

void gfx_fill_rect_rgb(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)screen_w) x1 = (int)screen_w;
    if (y1 > (int)screen_h) y1 = (int)screen_h;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = backbuffer + (uint32_t)yy * GFX_MAX_W + (uint32_t)x0;
        for (int xx = x0; xx < x1; xx++) {
            *row++ = color;
        }
    }
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    gfx_fill_rect_rgb(x, y, w, h, legacy_lut[color]);
}

void gfx_hline_rgb(int x, int y, int w, uint16_t color)
{
    gfx_fill_rect_rgb(x, y, w, 1, color);
}

void gfx_vline_rgb(int x, int y, int h, uint16_t color)
{
    gfx_fill_rect_rgb(x, y, 1, h, color);
}

void gfx_hline(int x, int y, int w, uint8_t color)
{
    gfx_fill_rect(x, y, w, 1, color);
}

void gfx_vline(int x, int y, int h, uint8_t color)
{
    gfx_fill_rect(x, y, 1, h, color);
}

void gfx_fill_circle_rgb(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }

    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        gfx_hline_rgb(cx - x, cy + y, 2 * x + 1, color);
        gfx_hline_rgb(cx - x, cy - y, 2 * x + 1, color);
        gfx_hline_rgb(cx - y, cy + x, 2 * y + 1, color);
        gfx_hline_rgb(cx - y, cy - x, 2 * y + 1, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint8_t color)
{
    gfx_fill_circle_rgb(cx, cy, r, legacy_lut[color]);
}

void gfx_rect_rgb(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_hline_rgb(x, y, w, color);
    gfx_hline_rgb(x, y + h - 1, w, color);
    gfx_vline_rgb(x, y, h, color);
    gfx_vline_rgb(x + w - 1, y, h, color);
}

void gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    gfx_rect_rgb(x, y, w, h, legacy_lut[color]);
}

void gfx_fill_rounded_rect_rgb(int x, int y, int w, int h, int r, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        gfx_fill_rect_rgb(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) {
        gfx_fill_rect_rgb(x, y, w, h, color);
        return;
    }

    gfx_fill_rect_rgb(x + r, y, w - 2 * r, h, color);
    gfx_fill_rect_rgb(x, y + r, r, h - 2 * r, color);
    gfx_fill_rect_rgb(x + w - r, y + r, r, h - 2 * r, color);

    if (r >= 2) {
        gfx_fill_rect_rgb(x + 1, y + 1, r - 1, r - 1, color);
        gfx_fill_rect_rgb(x + w - r, y + 1, r - 1, r - 1, color);
        gfx_fill_rect_rgb(x + 1, y + h - r, r - 1, r - 1, color);
        gfx_fill_rect_rgb(x + w - r, y + h - r, r - 1, r - 1, color);
    }
}

void gfx_fill_rounded_rect(int x, int y, int w, int h, int r, uint8_t color)
{
    gfx_fill_rounded_rect_rgb(x, y, w, h, r, legacy_lut[color]);
}

void gfx_rect_rounded_rgb(int x, int y, int w, int h, int r, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        gfx_rect_rgb(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) {
        gfx_rect_rgb(x, y, w, h, color);
        return;
    }

    gfx_hline_rgb(x + r, y, w - 2 * r, color);
    gfx_hline_rgb(x + r, y + h - 1, w - 2 * r, color);
    gfx_vline_rgb(x, y + r, h - 2 * r, color);
    gfx_vline_rgb(x + w - 1, y + r, h - 2 * r, color);

    gfx_pixel_rgb(x + 1, y + 1, color);
    gfx_pixel_rgb(x + w - 2, y + 1, color);
    gfx_pixel_rgb(x + 1, y + h - 2, color);
    gfx_pixel_rgb(x + w - 2, y + h - 2, color);
}

void gfx_rect_rounded(int x, int y, int w, int h, int r, uint8_t color)
{
    gfx_rect_rounded_rgb(x, y, w, h, r, legacy_lut[color]);
}

void gfx_shadow(int x, int y, int w, int h, uint8_t color)
{
    gfx_fill_rect(x + 3, y + h, w - 1, 2, color);
    gfx_fill_rect(x + w, y + 3, 2, h, color);
}

/* ------------------------------------------------------------------------- */
/* 16-bit karisim ve gradyan yardimcilari                                    */
/* ------------------------------------------------------------------------- */

static int expand5(int v)
{
    return (v * 255 + 15) / 31;
}

static int expand6(int v)
{
    return (v * 255 + 31) / 63;
}

uint16_t gfx_blend(uint16_t dst, uint16_t src, uint8_t alpha)
{
    if (alpha == 0) return dst;
    if (alpha >= 255) return src;

    int dr = expand5((dst >> 11) & 0x1F);
    int dg = expand6((dst >> 5) & 0x3F);
    int db = expand5(dst & 0x1F);
    int sr = expand5((src >> 11) & 0x1F);
    int sg = expand6((src >> 5) & 0x3F);
    int sb = expand5(src & 0x1F);

    int inv = 255 - alpha;
    int r = (dr * inv + sr * alpha) / 255;
    int g = (dg * inv + sg * alpha) / 255;
    int b = (db * inv + sb * alpha) / 255;

    return pack_rgb(r, g, b);
}

static void blend_pixel(int x, int y, uint16_t color, uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) {
        return;
    }
    uint32_t pos = (uint32_t)y * GFX_MAX_W + (uint32_t)x;
    backbuffer[pos] = gfx_blend(backbuffer[pos], color, alpha);
}

static void blend_hline(int x, int y, int w, uint16_t color, uint8_t alpha)
{
    for (int i = 0; i < w; i++) {
        blend_pixel(x + i, y, color, alpha);
    }
}

static void blend_vline(int x, int y, int h, uint16_t color, uint8_t alpha)
{
    for (int i = 0; i < h; i++) {
        blend_pixel(x, y + i, color, alpha);
    }
}

void gfx_blend_rect(int x, int y, int w, int h, uint16_t color, uint8_t alpha)
{
    if (w <= 0 || h <= 0 || alpha == 0) {
        return;
    }
    if (alpha >= 255) {
        gfx_fill_rect_rgb(x, y, w, h, color);
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)screen_w) x1 = (int)screen_w;
    if (y1 > (int)screen_h) y1 = (int)screen_h;

    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            blend_pixel(xx, yy, color, alpha);
        }
    }
}

static void rect_rounded_blend(int x, int y, int w, int h, int r,
                               uint16_t color, uint8_t alpha)
{
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        gfx_blend_rect(x, y, w, h, color, alpha);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    blend_hline(x + r, y, w - 2 * r, color, alpha);
    blend_hline(x + r, y + h - 1, w - 2 * r, color, alpha);
    blend_vline(x, y + r, h - 2 * r, color, alpha);
    blend_vline(x + w - 1, y + r, h - 2 * r, color, alpha);

    blend_pixel(x + 1, y + 1, color, alpha);
    blend_pixel(x + w - 2, y + 1, color, alpha);
    blend_pixel(x + 1, y + h - 2, color, alpha);
    blend_pixel(x + w - 2, y + h - 2, color, alpha);
}

void gfx_fill_circle_blend(int cx, int cy, int r, uint16_t color, uint8_t alpha)
{
    if (r <= 0 || alpha == 0) {
        return;
    }

    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        blend_hline(cx - x, cy + y, 2 * x + 1, color, alpha);
        blend_hline(cx - x, cy - y, 2 * x + 1, color, alpha);
        blend_hline(cx - y, cy + x, 2 * y + 1, color, alpha);
        blend_hline(cx - y, cy - x, 2 * y + 1, color, alpha);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_shadow_soft(int x, int y, int w, int h)
{
    /* 9 katmanli, asagi-saga egimli yumusak golge. */
    for (int i = 9; i >= 1; i--) {
        int alpha = 6 + (9 - i) * 4;
        rect_rounded_blend(x - i + 3, y - i + 5,
                           w + 2 * i, h + 2 * i,
                           6 + i, gfx_rgb(0, 0, 0), (uint8_t)alpha);
    }
}

void gfx_gradient_v(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    int tr = expand5((top >> 11) & 0x1F);
    int tg = expand6((top >> 5) & 0x3F);
    int tb = expand5(top & 0x1F);
    int br = expand5((bottom >> 11) & 0x1F);
    int bg = expand6((bottom >> 5) & 0x3F);
    int bb = expand5(bottom & 0x1F);

    static const int8_t dither[4] = { 0, 1, 0, -1 };

    for (int yy = 0; yy < h; yy++) {
        int t = (h == 1) ? 0 : (yy * 255) / (h - 1);
        int r = tr + ((br - tr) * t) / 255;
        int g = tg + ((bg - tg) * t) / 255;
        int b = tb + ((bb - tb) * t) / 255;

        r += dither[yy & 3];
        g += dither[(yy + 1) & 3];
        b += dither[(yy + 2) & 3];

        gfx_hline_rgb(x, y + yy, w, pack_rgb(r, g, b));
    }
}

void gfx_fill_rounded_rect_gradient_v(int x, int y, int w, int h, int r,
                                      uint16_t top, uint16_t bottom)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    gfx_fill_rounded_rect_rgb(x, y, w, h, r, top);

    if (w > 2 && h > 2) {
        gfx_gradient_v(x + 1, y + 1, w - 2, h - 2, top, bottom);
    }

    if (h >= 2 && w > 2 * r) {
        gfx_hline_rgb(x + r, y + h - 1, w - 2 * r, bottom);
    }
}

void gfx_char(int x, int y, unsigned char c, uint8_t fg, uint8_t bg)
{
    if (!font_addr) {
        return;
    }

    const uint8_t *glyph = tr_glyph(c);
    if (!glyph) {
        glyph = (const uint8_t *)(uintptr_t)(font_addr + (uint32_t)c * 8u);
    }

    uint16_t fg_rgb = legacy_lut[fg];
    uint16_t bg_rgb = legacy_lut[bg];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            if (bits & (uint8_t)(0x80u >> col)) {
                gfx_pixel_rgb(x + col, y + row, fg_rgb);
            } else if (bg != 0xFF) {
                gfx_pixel_rgb(x + col, y + row, bg_rgb);
            }
        }
    }
}

void gfx_text(int x, int y, const char *text, uint8_t fg, uint8_t bg)
{
    while (*text) {
        if (*text == '\n') {
            x = 0;
            y += 8;
        } else {
            gfx_char(x, y, (unsigned char)*text, fg, bg);
            x += 8;
        }
        text++;
    }
}
