#include "ui.h"
#include "bootinfo.h"
#include "console.h"
#include "gfx.h"
#include "heap.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "paging.h"
#include "pmm.h"
#include "rtc.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "types.h"
#include "vfs.h"
#include "vga.h"

/* ========================================================================= */
/* Terminal Gridi                                                            */
/* ========================================================================= */
#define TERM_ROWS 40
#define TERM_COLS 65

static char    term_chars[TERM_ROWS][TERM_COLS];
static uint8_t term_fg[TERM_ROWS][TERM_COLS];
static uint8_t term_bg[TERM_ROWS][TERM_COLS];

static int     term_row;
static int     term_col;
static uint8_t current_fg = VGA_LIGHT_GREEN;
static uint8_t current_bg = VGA_BLACK;

/* ========================================================================= */
/* Pencere Yapisi (9 Adet Uygulama)                                          */
/* ========================================================================= */
typedef struct {
    int id;
    const char *title;
    int x, y, w, h;
    int is_visible;
    int is_minimized;
    int is_active;
    int is_maximized;
    int restore_x, restore_y, restore_w, restore_h;
} window_t;

static window_t windows[UI_WIN_COUNT] = {
    { UI_WIN_TERMINAL, "casrev-os Terminal",  88,  24, 538, 360, 1, 0, 1, 0, 0, 0, 0, 0 },
    { UI_WIN_SYSINFO,  "Sistem Bilgisi",     380,  50, 390, 340, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_CALC,     "Hesap Makinesi",     240,  70, 240, 290, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_EXPLORER, "Dosya Gezgini",      160,  40, 480, 340, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_NOTEPAD,  "Not Defteri",        200,  60, 440, 330, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_PAINT,    "Paint / Cizim",      180,  50, 420, 350, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_MINES,    "Mayin Tarlasi",      260,  60, 260, 320, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_SETTINGS, "Ayarlar",            300,  80, 380, 330, 0, 0, 0, 0, 0, 0, 0, 0 },
    { UI_WIN_SEARCH,   "Arama",              140,  50, 520, 420, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* Masaustu duvar kagidi: 0=Gece, 1=Okyanus, 2=Gun Batimi, 3=Klasik */
static int wallpaper_mode;
static const char *wallpaper_names[4] = {
    "Gece", "Okyanus", "Gun Batimi", "Klasik"
};

/* Drag & Drop Durumu */
static int drag_win = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;
static int last_title_click_win = -1;
static uint32_t last_title_click_ticks;

/* Baslat Menusu */
static int start_menu_open = 0;

/* UI Genel Durum Bilgileri */
static int      ui_dirty = 1;
static char     clock_text[12] = "--:--:--";
static char     mouse_text[20] = "FARE: ---,---";
static uint32_t last_second = 0xFFFFFFFFu;
static int      last_mouse_x = -1;
static int      last_mouse_y = -1;
static int      last_buttons = 0;

/* ========================================================================= */
/* 1. Hesap Makinesi Durumu                                                  */
/* ========================================================================= */
static char    calc_display[32] = "0";
static int32_t calc_operand1 = 0;
static char    calc_op = 0;
static int     calc_new_number = 1;

static void calc_input_char(char c)
{
    if (c >= '0' && c <= '9') {
        if (calc_new_number || (calc_display[0] == '0' && calc_display[1] == '\0')) {
            calc_display[0] = c;
            calc_display[1] = '\0';
            calc_new_number = 0;
        } else {
            uint32_t len = strlen(calc_display);
            if (len < 14) {
                calc_display[len] = c;
                calc_display[len + 1] = '\0';
            }
        }
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        int32_t val = 0;
        int sign = 1;
        const char *p = calc_display;
        if (*p == '-') { sign = -1; p++; }
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        calc_operand1 = val * sign;
        calc_op = c;
        calc_new_number = 1;
    } else if (c == '=' || c == '\n') {
        if (calc_op != 0) {
            int32_t op2 = 0;
            int sign = 1;
            const char *p = calc_display;
            if (*p == '-') { sign = -1; p++; }
            while (*p >= '0' && *p <= '9') { op2 = op2 * 10 + (*p - '0'); p++; }
            op2 *= sign;

            int32_t res = 0;
            if (calc_op == '+') res = calc_operand1 + op2;
            else if (calc_op == '-') res = calc_operand1 - op2;
            else if (calc_op == '*') res = calc_operand1 * op2;
            else if (calc_op == '/') {
                if (op2 != 0) res = calc_operand1 / op2;
                else {
                    strcpy(calc_display, "HATA: Sifira Bol");
                    calc_op = 0;
                    calc_new_number = 1;
                    return;
                }
            }

            char buf[32];
            int idx = 0;
            uint32_t uval = (res < 0) ? (uint32_t)(-res) : (uint32_t)res;

            if (uval == 0) {
                buf[idx++] = '0';
            } else {
                char temp[32];
                int tidx = 0;
                while (uval > 0) {
                    temp[tidx++] = (char)('0' + (uval % 10));
                    uval /= 10;
                }
                if (res < 0) buf[idx++] = '-';
                while (tidx > 0) {
                    buf[idx++] = temp[--tidx];
                }
            }
            buf[idx] = '\0';
            strcpy(calc_display, buf);

            calc_operand1 = res;
            calc_op = 0;
            calc_new_number = 1;
        }
    } else if (c == 'c' || c == 'C') {
        strcpy(calc_display, "0");
        calc_operand1 = 0;
        calc_op = 0;
        calc_new_number = 1;
    }
}

/* ========================================================================= */
/* 2. Dosya Gezgini (File Explorer) Durumu                                   */
/* ========================================================================= */
static vfs_node_t *explorer_dir = 0;

/* ========================================================================= */
/* 3. Not Defteri (Notepad) Durumu                                           */
/* ========================================================================= */
#define NOTEPAD_MAX 1024
static char        notepad_buf[NOTEPAD_MAX] = "Yeni notunuzu buraya yazin...\n";
static uint32_t    notepad_len = 32;
static uint32_t    notepad_cursor = 32;
static vfs_node_t *notepad_current_node = 0;
static char        notepad_filename[32] = "yeni_not.txt";

void ui_notepad_open_file(const char *path)
{
    if (!path) return;
    vfs_node_t *node = vfs_resolve_path(path);
    if (node && node->type == VFS_TYPE_FILE) {
        notepad_current_node = node;
        strcpy(notepad_filename, node->name);
        uint32_t len = node->size;
        if (len >= NOTEPAD_MAX) len = NOTEPAD_MAX - 1;
        if (node->content) {
            memcpy(notepad_buf, node->content, len);
        } else {
            len = 0;
        }
        notepad_buf[len] = '\0';
        notepad_len = len;
        notepad_cursor = len;
    }
}

static void notepad_save(void)
{
    if (!notepad_current_node) {
        notepad_current_node = vfs_create_file(vfs_get_cwd(), notepad_filename, notepad_buf);
    } else {
        vfs_write_file(notepad_current_node, notepad_buf, notepad_len);
    }
}

/* ========================================================================= */
/* 4. Paint / Cizim Tahtasi Durumu                                           */
/* ========================================================================= */
#define PAINT_W 396
#define PAINT_H 260
static uint8_t paint_canvas[PAINT_W * PAINT_H];
static uint8_t paint_color = VGA_LIGHT_CYAN;
static int     paint_brush = 2;
static int     paint_is_eraser = 0;

static void paint_init(void)
{
    memset(paint_canvas, GFX_WHITE, sizeof(paint_canvas));
}

static void paint_plot(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= PAINT_W || cy >= PAINT_H) return;

    uint8_t col = paint_is_eraser ? GFX_WHITE : paint_color;
    int r = paint_brush;

    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px >= 0 && px < PAINT_W && py >= 0 && py < PAINT_H) {
                paint_canvas[py * PAINT_W + px] = col;
            }
        }
    }
}

/* ========================================================================= */
/* 5. Mayin Tarlasi (Minesweeper) Durumu                                     */
/* ========================================================================= */
#define MINES_COLS 9
#define MINES_ROWS 9
#define MINES_TOTAL 10

static int mines_map[MINES_ROWS][MINES_COLS];   /* -1 = mayin, 0..8 = komsuluk */
static int mines_state[MINES_ROWS][MINES_COLS]; /* 0 = kapali, 1 = acik, 2 = bayrak */
static int mines_status = 0;                    /* 0 = oyun devam, 1 = kazandi, 2 = patladi */
static int mines_remaining = MINES_TOTAL;
static int mines_init_done = 0;

static void mines_init_game(void)
{
    memset(mines_map, 0, sizeof(mines_map));
    memset(mines_state, 0, sizeof(mines_state));
    mines_status = 0;
    mines_remaining = MINES_TOTAL;

    /* Mayinlari rastgele yerlestir */
    uint32_t seed = timer_ticks() + 17;
    int placed = 0;

    while (placed < MINES_TOTAL) {
        seed = seed * 1103515245 + 12345;
        int r = (int)((seed >> 16) % MINES_ROWS);
        seed = seed * 1103515245 + 12345;
        int c = (int)((seed >> 16) % MINES_COLS);

        if (mines_map[r][c] != -1) {
            mines_map[r][c] = -1;
            placed++;
        }
    }

    /* Komsuluk sayilarini hesapla */
    for (int r = 0; r < MINES_ROWS; r++) {
        for (int c = 0; c < MINES_COLS; c++) {
            if (mines_map[r][c] == -1) continue;

            int count = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int nr = r + dr;
                    int nc = c + dc;
                    if (nr >= 0 && nr < MINES_ROWS && nc >= 0 && nc < MINES_COLS) {
                        if (mines_map[nr][nc] == -1) count++;
                    }
                }
            }
            mines_map[r][c] = count;
        }
    }

    mines_init_done = 1;
}

static void mines_reveal(int r, int c)
{
    if (r < 0 || r >= MINES_ROWS || c < 0 || c >= MINES_COLS) return;
    if (mines_state[r][c] != 0 || mines_status != 0) return;

    mines_state[r][c] = 1;

    if (mines_map[r][c] == -1) {
        /* Patladi! Tum mayinlari ac */
        mines_status = 2;
        for (int rr = 0; rr < MINES_ROWS; rr++) {
            for (int cc = 0; cc < MINES_COLS; cc++) {
                if (mines_map[rr][cc] == -1) mines_state[rr][cc] = 1;
            }
        }
        return;
    }

    /* 0 ise etraftaki bos hucreleri ac */
    if (mines_map[r][c] == 0) {
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                mines_reveal(r + dr, c + dc);
            }
        }
    }

    /* Kazanma kontrolu */
    int hidden_safe = 0;
    for (int rr = 0; rr < MINES_ROWS; rr++) {
        for (int cc = 0; cc < MINES_COLS; cc++) {
            if (mines_map[rr][cc] != -1 && mines_state[rr][cc] != 1) {
                hidden_safe++;
            }
        }
    }
    if (hidden_safe == 0) {
        mines_status = 1;
    }
}

static void mines_toggle_flag(int r, int c)
{
    if (r < 0 || r >= MINES_ROWS || c < 0 || c >= MINES_COLS || mines_status != 0) return;
    if (mines_state[r][c] == 0) {
        mines_state[r][c] = 2;
        if (mines_remaining > 0) mines_remaining--;
    } else if (mines_state[r][c] == 2) {
        mines_state[r][c] = 0;
        mines_remaining++;
    }
}

/* ========================================================================= */
/* Terminal Metin Islemleri                                                  */
/* ========================================================================= */
static void term_scroll(void)
{
    memmove(term_chars[0], term_chars[1], (TERM_ROWS - 1) * TERM_COLS);
    memmove(term_fg[0], term_fg[1], (TERM_ROWS - 1) * TERM_COLS);
    memmove(term_bg[0], term_bg[1], (TERM_ROWS - 1) * TERM_COLS);

    for (int c = 0; c < TERM_COLS; c++) {
        term_chars[TERM_ROWS - 1][c] = ' ';
        term_fg[TERM_ROWS - 1][c] = VGA_LIGHT_GREEN;
        term_bg[TERM_ROWS - 1][c] = VGA_BLACK;
    }
}

void ui_term_clear(void)
{
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            term_chars[r][c] = ' ';
            term_fg[r][c] = VGA_LIGHT_GREEN;
            term_bg[r][c] = VGA_BLACK;
        }
    }
    term_row = 0;
    term_col = 0;
    current_fg = VGA_LIGHT_GREEN;
    current_bg = VGA_BLACK;
    ui_dirty = 1;
}

void ui_term_set_color(uint8_t fg, uint8_t bg)
{
    current_fg = (uint8_t)(fg & 0x0Fu);
    current_bg = (uint8_t)(bg & 0x0Fu);
}

void ui_term_putc(char c)
{
    if (c == '\n') {
        term_col = 0;
        term_row++;
    } else if (c == '\r') {
        term_col = 0;
    } else if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            term_chars[term_row][term_col] = ' ';
        }
    } else if (c == '\t') {
        do {
            ui_term_putc(' ');
        } while ((term_col & 3) != 0);
        return;
    } else {
        if (term_col >= TERM_COLS) {
            term_col = 0;
            term_row++;
        }
        if (term_row >= TERM_ROWS) {
            term_scroll();
            term_row = TERM_ROWS - 1;
        }

        term_chars[term_row][term_col] = c;
        term_fg[term_row][term_col] = current_fg;
        term_bg[term_row][term_col] = current_bg;
        term_col++;

        if (term_col >= TERM_COLS) {
            term_col = 0;
            term_row++;
        }
    }

    if (term_row >= TERM_ROWS) {
        term_scroll();
        term_row = TERM_ROWS - 1;
    }

    ui_dirty = 1;
}

/* ========================================================================= */
/* 6. Yerel Arama (Search) Durumu                                            */
/* ========================================================================= */
#define SEARCH_QUERY_MAX   40
#define SEARCH_MAX_RESULTS 5
#define SEARCH_MAX_LOCAL   5
#define SEARCH_MAX_NET     5
#define SEARCH_TITLE_MAX   80
#define SEARCH_META_MAX    72
#define SEARCH_DETAIL_MAX  140
#define SEARCH_PATH_MAX    160

#define SEARCH_PAD         24
#define SEARCH_FIELD_TOP   72
#define SEARCH_FIELD_H     32
#define SEARCH_BTN_W       80
#define SEARCH_RESULTS_TOP 128
#define SEARCH_ROW_H       44

#define SEARCH_TYPE_FILE 1
#define SEARCH_TYPE_APP  2
#define SEARCH_TYPE_CMD  3
#define SEARCH_TYPE_NET  4

#define SEARCH_NET_OFF     0
#define SEARCH_NET_IDLE    1
#define SEARCH_NET_PENDING 2
#define SEARCH_NET_WAITING 3
#define SEARCH_NET_READY   4
#define SEARCH_NET_ERROR   5

#define SEARCH_NET_LINE_MAX 512
#define SEARCH_NET_TIMEOUT  800   /* 8 saniye (100 Hz) */

typedef struct {
    int type;
    int action;
    char icon[3];
    uint8_t icon_color;
    char title[SEARCH_TITLE_MAX];
    char meta[SEARCH_META_MAX];
    char detail[SEARCH_DETAIL_MAX];
    char path[SEARCH_PATH_MAX];
} search_result_t;

typedef struct {
    const char *name;
    const char *detail;
    const char *icon;
    uint8_t color;
    int win_id;
} search_app_t;

typedef struct {
    const char *name;
    const char *detail;
} search_cmd_t;

static const search_app_t search_apps[] = {
    { "Terminal",        "Kabuk, komut gecmisi ve cikti",          ">_", UI_ACCENT,        UI_WIN_TERMINAL },
    { "Dosya Gezgini",   "RamFS klasorlerinde gezinme",            "[]", VGA_YELLOW,       UI_WIN_EXPLORER },
    { "Not Defteri",     "Metin dosyasi acma ve duzenleme",        "N",  VGA_LIGHT_CYAN,   UI_WIN_NOTEPAD },
    { "Paint",           "Serbest cizim ve firca araclari",        "P",  VGA_LIGHT_RED,    UI_WIN_PAINT },
    { "Mayin Tarlasi",   "9x9 klasik mayin oyunu",                 "*",  VGA_YELLOW,       UI_WIN_MINES },
    { "Hesap Makinesi",  "Dort islem, fare ve klavye destegi",     "#",  UI_ACCENT,        UI_WIN_CALC },
    { "Sistem Bilgisi",  "CPU, RAM, RTC ve uptime bilgileri",      "i",  UI_ACCENT,        UI_WIN_SYSINFO },
    { "Ayarlar",         "Duvar kagidi, klavye ve tercihler",      "A",  VGA_LIGHT_GREEN,  UI_WIN_SETTINGS }
};

static const search_cmd_t search_commands[] = {
    { "help",     "Komut listesini gosterir" },
    { "clear",    "Terminali temizler" },
    { "ls",       "Dizindeki dosya ve klasorleri listeler" },
    { "cd",       "Dizini degistirir" },
    { "pwd",      "Gecerli dizini yazdirir" },
    { "cat",      "Dosya icerigini okur" },
    { "mkdir",    "Yeni klasor olusturur" },
    { "touch",    "Yeni bos dosya olusturur" },
    { "explorer", "Dosya Gezginini acar" },
    { "notepad",  "Not Defterini acar" },
    { "paint",    "Paint cizim tahtasini acar" },
    { "mines",    "Mayin Tarlasi oyununu acar" },
    { "calc",     "Hesap Makinesini acar" },
    { "sysinfo",  "Sistem Bilgisi penceresini acar" },
    { "settings", "Ayarlar penceresini acar" },
    { "ayarlar",  "Ayarlar penceresini acar" },
    { "search",   "Arama penceresini acar (yerel + internet)" },
    { "arama",    "Arama penceresini acar (yerel + internet)" },
    { "launcher", "Baslat menusunu acar veya kapatir" },
    { "uptime",   "Calisma suresini gosterir" },
    { "mem",      "Bellek istatistiklerini gosterir" },
    { "frame",    "Fiziksel kare testi yapar" },
    { "heap",     "kmalloc ve kfree testi yapar" },
    { "time",     "RTC tarih ve saatini gosterir" },
    { "mouse",    "Fare konumunu gosterir" },
    { "about",    "Sistem bilgisini gosterir" },
    { "reboot",   "Sistemi yeniden baslatir" },
    { "halt",     "Sistemi durdurur" }
};

static char search_query[SEARCH_QUERY_MAX + 1];
static int  search_query_len;

/* Yerel sonuclar, internet sonuclari ve ekranda birlestirilmis liste. */
static search_result_t search_local_results[SEARCH_MAX_LOCAL];
static int  search_local_count;
static int  search_local_total;
static search_result_t search_net_results[SEARCH_MAX_NET];
static int  search_net_count;
static int  search_net_total;
static search_result_t search_results[SEARCH_MAX_RESULTS];
static int  search_result_count;
static int  search_total;

static int      search_net_state = SEARCH_NET_OFF;
static int      search_net_seq;
static int      search_net_resp_id = -1;
static uint32_t search_net_send_tick;
static uint32_t search_net_wait_start;
static char     search_net_line[SEARCH_NET_LINE_MAX];
static int      search_net_line_len;
static int      search_detail = -1;

static void search_copy(char *dst, const char *src, int max)
{
    if (!dst || max <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static char search_fold(unsigned char c)
{
    switch (c) {
    case 0xC7: case 0xE7: return 'c';
    case 0xD0: case 0xF0: return 'g';
    case 0xDD: case 0xFD: return 'i';
    case 0xD6: case 0xF6: return 'o';
    case 0xDE: case 0xFE: return 's';
    case 0xDC: case 0xFC: return 'u';
    default:
        if (c >= 'A' && c <= 'Z') return (char)(c + 32);
        return (char)c;
    }
}

static const char *search_find(const char *text, const char *needle)
{
    if (!text || !needle || !*needle) return text;
    for (const char *p = text; *p; p++) {
        const char *a = p;
        const char *b = needle;
        while (*a && *b && search_fold((unsigned char)*a) ==
                             search_fold((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) return p;
    }
    return 0;
}

static int search_contains(const char *text, const char *needle)
{
    return search_find(text, needle) != 0;
}

static void search_snippet(const char *content, const char *hit,
                           char *out, int max)
{
    if (!out || max <= 0) return;
    out[0] = '\0';
    if (!content) return;

    const char *start = content;
    if (hit && hit >= content) {
        start = hit;
        int back = 0;
        while (start > content && back < 24) {
            start--;
            if (*start == '\n' || *start == '\r') {
                start++;
                break;
            }
            back++;
        }
    }

    int pos = 0;
    if (start > content && max > 8) {
        out[pos++] = '.';
        out[pos++] = '.';
        out[pos++] = '.';
        out[pos++] = ' ';
    }

    const char *p = start;
    while (*p && pos < max - 1) {
        if (*p == '\n' || *p == '\r') break;
        out[pos++] = (*p == '\t') ? ' ' : *p;
        p++;
    }
    while (pos > 0 && out[pos - 1] == ' ') pos--;

    if (*p && *p != '\n' && *p != '\r' && pos + 4 < max) {
        out[pos++] = '.';
        out[pos++] = '.';
        out[pos++] = '.';
    }
    out[pos] = '\0';
}

static void search_copy_result(search_result_t *dst, const search_result_t *src)
{
    *dst = *src;
}

static void search_add_local(int type, const char *icon, uint8_t color,
                             const char *title, const char *meta,
                             const char *detail, const char *path, int action)
{
    search_local_total++;
    if (search_local_count >= SEARCH_MAX_LOCAL) return;

    search_result_t *r = &search_local_results[search_local_count++];
    r->type = type;
    r->action = action;
    r->icon_color = color;
    search_copy(r->icon, icon, sizeof(r->icon));
    search_copy(r->title, title, sizeof(r->title));
    search_copy(r->meta, meta, sizeof(r->meta));
    search_copy(r->detail, detail, sizeof(r->detail));
    search_copy(r->path, path, sizeof(r->path));
}

static void search_add_net(const char *title, const char *site,
                           const char *url, const char *detail)
{
    if (search_net_count >= SEARCH_MAX_NET) return;

    search_result_t *r = &search_net_results[search_net_count++];
    r->type = SEARCH_TYPE_NET;
    r->action = -1;
    r->icon_color = VGA_LIGHT_BLUE;
    search_copy(r->icon, "W", sizeof(r->icon));
    search_copy(r->title, title, sizeof(r->title));

    char meta[SEARCH_META_MAX];
    string_format(meta, sizeof(meta), "Internet | %s", site ? site : "web");
    search_copy(r->meta, meta, sizeof(r->meta));
    search_copy(r->detail, detail, sizeof(r->detail));
    search_copy(r->path, url, sizeof(r->path));
}

/* Yerel ve internet sonuclarini en fazla 5 satirda birlestirir. */
static void search_merge_results(void)
{
    search_result_count = 0;

    int local_limit = SEARCH_MAX_RESULTS;
    if (search_net_state == SEARCH_NET_READY && search_net_count > 0) {
        local_limit = 3;   /* internet sonuclarina yer ac */
    }
    if (local_limit > search_local_count) local_limit = search_local_count;

    for (int i = 0; i < local_limit && search_result_count < SEARCH_MAX_RESULTS; i++) {
        search_copy_result(&search_results[search_result_count++],
                           &search_local_results[i]);
    }

    if (search_net_state == SEARCH_NET_READY) {
        for (int i = 0; i < search_net_count && search_result_count < SEARCH_MAX_RESULTS; i++) {
            search_copy_result(&search_results[search_result_count++],
                               &search_net_results[i]);
        }
    }

    search_total = search_local_total;
    if (search_net_state == SEARCH_NET_READY) {
        search_total += search_net_total;
    }
}

static void search_scan_dir(vfs_node_t *dir, const char *query)
{
    if (!dir) return;

    for (vfs_node_t *node = dir->first_child; node; node = node->next_sibling) {
        if (node->type == VFS_TYPE_DIR) {
            search_scan_dir(node, query);
            continue;
        }
        if (node->type != VFS_TYPE_FILE) continue;

        char path[SEARCH_PATH_MAX];
        vfs_get_path(node, path, sizeof(path));

        const char *hit = 0;
        if (node->content && node->size) {
            hit = search_find(node->content, query);
        }

        if (!search_contains(node->name, query) &&
            !search_contains(path, query) && !hit) {
            continue;
        }

        char meta[SEARCH_META_MAX];
        string_format(meta, sizeof(meta), "%s | %u bayt", path, node->size);

        char detail[SEARCH_DETAIL_MAX];
        search_snippet(node->content, hit, detail, sizeof(detail));
        if (!detail[0]) {
            search_copy(detail, "RamFS dosyasi", sizeof(detail));
        }

        search_add_local(SEARCH_TYPE_FILE, "D", VGA_LIGHT_CYAN, node->name,
                         meta, detail, path, -1);
    }
}

static int search_net_parse_int(const char *s)
{
    int sign = 1;
    int value = 0;

    if (!s) return 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}

static int search_net_split(char *line, char **fields, int max)
{
    int count = 0;
    if (!line || max <= 0) return 0;

    fields[count++] = line;
    for (char *p = line; *p && count < max; p++) {
        if (*p == '\t') {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return count;
}

static void search_net_handle_line(char *line)
{
    char *fields[6];
    int count = search_net_split(line, fields, 6);
    if (count < 1) return;

    if (strcmp(fields[0], "R") == 0 && count >= 3) {
        int id = search_net_parse_int(fields[1]);
        if (id != search_net_seq) {
            search_net_resp_id = -1;
            return;
        }
        search_net_resp_id = id;
        search_net_count = 0;
        search_net_total = search_net_parse_int(fields[2]);
        if (search_net_total < 0) search_net_total = 0;
        search_net_state = SEARCH_NET_WAITING;
        search_net_wait_start = timer_ticks();
    } else if (strcmp(fields[0], "T") == 0 && count >= 5) {
        if (search_net_resp_id != search_net_seq) return;
        search_add_net(fields[1], fields[2], fields[3], fields[4]);
        search_net_wait_start = timer_ticks();
    } else if (strcmp(fields[0], "END") == 0 && count >= 2) {
        if (search_net_parse_int(fields[1]) != search_net_seq) return;
        search_net_resp_id = -1;
        search_net_state = SEARCH_NET_READY;
        search_net_total = search_net_count;
        search_merge_results();
        ui_dirty = 1;
    } else if (strcmp(fields[0], "E") == 0 && count >= 2) {
        if (search_net_parse_int(fields[1]) != search_net_seq) return;
        search_net_resp_id = -1;
        search_net_state = SEARCH_NET_ERROR;
        search_merge_results();
        ui_dirty = 1;
    }
}

static void search_net_reset(void)
{
    search_net_seq++;
    search_net_resp_id = -1;
    search_net_count = 0;
    search_net_total = 0;
    search_net_line_len = 0;
    search_detail = -1;

    if (!search_query[0]) {
        search_net_state = serial2_present() ? SEARCH_NET_IDLE : SEARCH_NET_OFF;
        return;
    }
    if (!serial2_present()) {
        search_net_state = SEARCH_NET_OFF;
        return;
    }

    /* Tus vuruslari durulsun; son sorguyu 400 ms sonra gonder. */
    search_net_state = SEARCH_NET_PENDING;
    search_net_send_tick = timer_ticks() + 40;
}

static void search_net_send(void)
{
    char line[64];
    string_format(line, sizeof(line), "Q\t%d\t%s\n", search_net_seq, search_query);
    serial2_write(line);
    search_net_resp_id = -1;
    search_net_state = SEARCH_NET_WAITING;
    search_net_wait_start = timer_ticks();
}

static void search_poll_net(void)
{
    if (!serial2_present()) {
        if (search_net_state != SEARCH_NET_OFF &&
            search_net_state != SEARCH_NET_IDLE) {
            search_net_state = SEARCH_NET_OFF;
            search_merge_results();
            ui_dirty = 1;
        }
        return;
    }

    int c;
    while ((c = serial2_read()) >= 0) {
        if (c == '\n') {
            search_net_line[search_net_line_len] = '\0';
            if (search_net_line_len > 0) {
                search_net_handle_line(search_net_line);
            }
            search_net_line_len = 0;
        } else if (c != '\r') {
            if (search_net_line_len < SEARCH_NET_LINE_MAX - 1) {
                search_net_line[search_net_line_len++] = (char)c;
            } else {
                search_net_line_len = 0;   /* asiri uzun satiri at */
            }
        }
    }

    uint32_t now = timer_ticks();
    if (search_net_state == SEARCH_NET_PENDING &&
        (int32_t)(now - search_net_send_tick) >= 0) {
        search_net_send();
    } else if (search_net_state == SEARCH_NET_WAITING &&
               (uint32_t)(now - search_net_wait_start) > SEARCH_NET_TIMEOUT) {
        search_net_state = SEARCH_NET_ERROR;
        search_net_resp_id = -1;
        search_merge_results();
        ui_dirty = 1;
    }
}

static void search_run(void)
{
    search_local_count = 0;
    search_local_total = 0;

    if (!search_query[0]) {
        /* Bos sorguda hizli erisim kartlari. */
        for (uint32_t i = 0; i < sizeof(search_apps) / sizeof(search_apps[0]); i++) {
            search_add_local(SEARCH_TYPE_APP, search_apps[i].icon, search_apps[i].color,
                             search_apps[i].name, "Uygulama | tikla ac",
                             search_apps[i].detail, "", search_apps[i].win_id);
        }
        search_net_reset();
        search_merge_results();
        return;
    }

    /* 1. Uygulamalar */
    for (uint32_t i = 0; i < sizeof(search_apps) / sizeof(search_apps[0]); i++) {
        if (search_contains(search_apps[i].name, search_query) ||
            search_contains(search_apps[i].detail, search_query)) {
            search_add_local(SEARCH_TYPE_APP, search_apps[i].icon, search_apps[i].color,
                             search_apps[i].name, "Uygulama | tikla ac",
                             search_apps[i].detail, "", search_apps[i].win_id);
        }
    }

    /* 2. RamFS dosyalari: ad, yol ve icerik */
    search_scan_dir(vfs_get_root(), search_query);

    /* 3. Kabuk komutlari */
    for (uint32_t i = 0; i < sizeof(search_commands) / sizeof(search_commands[0]); i++) {
        if (search_contains(search_commands[i].name, search_query) ||
            search_contains(search_commands[i].detail, search_query)) {
            search_add_local(SEARCH_TYPE_CMD, ">", UI_ACCENT, search_commands[i].name,
                             "Komut | Terminalde calistirin",
                             search_commands[i].detail, "", -1);
        }
    }

    /* 4. Internet: host koprusune sorgu planla. */
    search_net_reset();
    search_merge_results();
}

static void search_activate(int index)
{
    if (index < 0 || index >= search_result_count) return;

    search_result_t *r = &search_results[index];
    if (r->type == SEARCH_TYPE_NET) {
        search_detail = index;
        ui_dirty = 1;
        return;
    }

    if (r->type == SEARCH_TYPE_FILE && r->path[0]) {
        ui_notepad_open_file(r->path);
        ui_window_open(UI_WIN_NOTEPAD);
    } else if (r->type == SEARCH_TYPE_APP) {
        ui_window_open(r->action);
    } else {
        ui_window_open(UI_WIN_TERMINAL);
    }
    ui_dirty = 1;
}

void ui_search_open(const char *query)
{
    if (query && *query) {
        search_copy(search_query, query, SEARCH_QUERY_MAX + 1);
        search_query_len = (int)strlen(search_query);
        if (search_query_len > SEARCH_QUERY_MAX) {
            search_query_len = SEARCH_QUERY_MAX;
            search_query[search_query_len] = '\0';
        }
    }

    ui_window_open(UI_WIN_SEARCH);
    ui_dirty = 1;
}

/* ========================================================================= */
/* Pencere Yonetimi                                                          */
/* ========================================================================= */
static void set_active_window(int win_id)
{
    for (int i = 0; i < UI_WIN_COUNT; i++) {
        windows[i].is_active = (windows[i].id == win_id);
    }
    ui_dirty = 1;
}

/* Odaklanan pencere kapanir/kucultulurse baska bir gorunur pencereye gec. */
static void activate_fallback_window(int exclude)
{
    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (windows[i].is_active && windows[i].is_visible && !windows[i].is_minimized) {
            return;
        }
    }

    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (i == exclude) continue;
        if (windows[i].is_visible && !windows[i].is_minimized) {
            set_active_window(windows[i].id);
            return;
        }
    }
}

void ui_window_open(int win_id)
{
    if (win_id < 0 || win_id >= UI_WIN_COUNT) return;
    if (win_id == UI_WIN_SEARCH) {
        search_run();
    }
    windows[win_id].is_visible = 1;
    windows[win_id].is_minimized = 0;
    set_active_window(win_id);
}

void ui_window_close(int win_id)
{
    if (win_id < 0 || win_id >= UI_WIN_COUNT) return;
    windows[win_id].is_visible = 0;
    windows[win_id].is_active = 0;
    activate_fallback_window(win_id);
    ui_dirty = 1;
}

void ui_window_toggle(int win_id)
{
    if (win_id < 0 || win_id >= UI_WIN_COUNT) return;
    if (!windows[win_id].is_visible) {
        ui_window_open(win_id);
    } else if (windows[win_id].is_minimized) {
        windows[win_id].is_minimized = 0;
        set_active_window(win_id);
    } else if (windows[win_id].is_active) {
        windows[win_id].is_minimized = 1;
        windows[win_id].is_active = 0;
        activate_fallback_window(win_id);
        ui_dirty = 1;
    } else {
        set_active_window(win_id);
    }
}

static void toggle_maximize(int win_id)
{
    if (win_id < 0 || win_id >= UI_WIN_COUNT) return;

    window_t *w = &windows[win_id];
    if (!w->is_maximized) {
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_w = w->w;
        w->restore_h = w->h;
        w->x = 0;
        w->y = 0;
        w->w = 800;
        w->h = 562;
        w->is_maximized = 1;
    } else {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->w = w->restore_w;
        w->h = w->restore_h;
        w->is_maximized = 0;
    }

    set_active_window(win_id);
    ui_dirty = 1;
}

void ui_start_menu_toggle(void)
{
    start_menu_open = !start_menu_open;
    ui_dirty = 1;
}

static int is_turkish_char(int key)
{
    switch (key) {
    case 0xC7: case 0xD0: case 0xD6: case 0xDC:
    case 0xDD: case 0xDE: case 0xE7: case 0xF0:
    case 0xF6: case 0xFC: case 0xFD: case 0xFE:
        return 1;
    default:
        return 0;
    }
}

int ui_handle_key(int key)
{
    if (key == 27) { /* ESC */
        ui_start_menu_toggle();
        return 1;
    }

    /* 1. Arama Penceresi Aktifse Sorgu Girisi */
    if (windows[UI_WIN_SEARCH].is_visible &&
        !windows[UI_WIN_SEARCH].is_minimized &&
        windows[UI_WIN_SEARCH].is_active) {
        if ((key >= 32 && key <= 126) || is_turkish_char(key)) {
            if (search_query_len < SEARCH_QUERY_MAX) {
                search_query[search_query_len++] = (char)key;
                search_query[search_query_len] = '\0';
                search_run();
                ui_dirty = 1;
            }
            return 1;
        } else if (key == '\b') {
            if (search_query_len > 0) {
                search_query[--search_query_len] = '\0';
                search_run();
                ui_dirty = 1;
            }
            return 1;
        } else if (key == '\n') {
            if (search_detail >= 0) {
                search_detail = -1;
                ui_dirty = 1;
                return 1;
            }
            if (search_query[0] && search_result_count > 0) {
                search_activate(0);
            }
            return 1;
        }
    }

    /* 2. Not Defteri Aktifse Klavye Metin Girisi */
    if (windows[UI_WIN_NOTEPAD].is_visible &&
        !windows[UI_WIN_NOTEPAD].is_minimized &&
        windows[UI_WIN_NOTEPAD].is_active) {
        if ((key >= 32 && key <= 126) || is_turkish_char(key)) {
            if (notepad_len + 1 < NOTEPAD_MAX) {
                /* Karakter ekle */
                for (int i = (int)notepad_len; i > (int)notepad_cursor; i--) {
                    notepad_buf[i] = notepad_buf[i - 1];
                }
                notepad_buf[notepad_cursor++] = (char)key;
                notepad_len++;
                notepad_buf[notepad_len] = '\0';
                ui_dirty = 1;
            }
            return 1;
        } else if (key == '\n') {
            if (notepad_len + 1 < NOTEPAD_MAX) {
                for (int i = (int)notepad_len; i > (int)notepad_cursor; i--) {
                    notepad_buf[i] = notepad_buf[i - 1];
                }
                notepad_buf[notepad_cursor++] = '\n';
                notepad_len++;
                notepad_buf[notepad_len] = '\0';
                ui_dirty = 1;
            }
            return 1;
        } else if (key == '\b') {
            if (notepad_cursor > 0) {
                for (uint32_t i = notepad_cursor - 1; i < notepad_len; i++) {
                    notepad_buf[i] = notepad_buf[i + 1];
                }
                notepad_cursor--;
                notepad_len--;
                ui_dirty = 1;
            }
            return 1;
        }
    }

    /* 3. Hesap Makinesi Aktifse Rakam & Islem */
    if (windows[UI_WIN_CALC].is_visible &&
        !windows[UI_WIN_CALC].is_minimized &&
        windows[UI_WIN_CALC].is_active) {
        if ((key >= '0' && key <= '9') ||
            key == '+' || key == '-' || key == '*' || key == '/' ||
            key == '=' || key == '\n' || key == 'c' || key == 'C') {
            calc_input_char((char)key);
            ui_dirty = 1;
            return 1;
        }
    }

    return 0;
}

/* ========================================================================= */
/* Fare ve Cizim Yardimcilari                                                */
/* ========================================================================= */
static void draw_mouse_cursor(int x, int y, int buttons)
{
    static const uint8_t arrow[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
        0xFE, 0xFF, 0xF8, 0xD8, 0x8C, 0x04
    };

    uint8_t color = (buttons & 0x01) ? VGA_YELLOW : VGA_WHITE;

    /* Koyu Golge */
    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 8; col++) {
            if (arrow[row] & (uint8_t)(0x80u >> col)) {
                gfx_pixel(x + col + 1, y + row + 1, UI_DARK_SHADOW);
            }
        }
    }

    /* Imlec Govdesi */
    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 8; col++) {
            if (arrow[row] & (uint8_t)(0x80u >> col)) {
                gfx_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_text_shadow(int x, int y, const char *text, uint8_t fg)
{
    gfx_text(x + 1, y + 1, text, GFX_BLACK, 0xFF);
    gfx_text(x, y, text, fg, 0xFF);
}

static void draw_window_header(window_t *win)
{
    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);

    /* Yumusak dusen golge; aktif pencereye hafif mavi isik. */
    gfx_shadow_soft(win->x, win->y, win->w, win->h);

    gfx_fill_rounded_rect_rgb(win->x, win->y, win->w, win->h, 5,
                              gfx_rgb(18, 21, 30));
    gfx_rect_rounded_rgb(win->x, win->y, win->w, win->h, 5,
                         win->is_active ? gfx_rgb(0, 138, 230)
                                        : gfx_rgb(48, 57, 74));

    uint16_t title_top;
    uint16_t title_bottom;
    if (win->is_active) {
        title_top = gfx_rgb(22, 86, 148);
        title_bottom = gfx_rgb(10, 48, 92);
    } else {
        title_top = gfx_rgb(45, 51, 64);
        title_bottom = gfx_rgb(28, 33, 43);
    }

    gfx_fill_rounded_rect_gradient_v(win->x, win->y, win->w, 24, 5,
                                     title_top, title_bottom);
    if (win->is_active) {
        gfx_hline_rgb(win->x + 5, win->y + 1, win->w - 10,
                      gfx_rgb(80, 190, 255));
    }
    gfx_hline_rgb(win->x + 1, win->y + 24, win->w - 2,
                  gfx_rgb(5, 8, 13));

    draw_text_shadow(win->x + 30, win->y + 8, win->title, VGA_WHITE);

    /* Uygulama ikonlari: renkli kenarli kucuk karolar */
    const char *icon = "?";
    uint8_t icon_col = UI_ACCENT;
    if (win->id == UI_WIN_TERMINAL)      { icon = ">"; icon_col = UI_ACCENT; }
    else if (win->id == UI_WIN_SYSINFO)  { icon = "i"; icon_col = UI_ACCENT; }
    else if (win->id == UI_WIN_CALC)     { icon = "="; icon_col = UI_ACCENT; }
    else if (win->id == UI_WIN_EXPLORER) { icon = "D"; icon_col = VGA_YELLOW; }
    else if (win->id == UI_WIN_NOTEPAD)  { icon = "N"; icon_col = VGA_LIGHT_CYAN; }
    else if (win->id == UI_WIN_PAINT)    { icon = "P"; icon_col = VGA_LIGHT_RED; }
    else if (win->id == UI_WIN_MINES)    { icon = "*"; icon_col = VGA_YELLOW; }
    else if (win->id == UI_WIN_SETTINGS) { icon = "A"; icon_col = VGA_LIGHT_GREEN; }
    else if (win->id == UI_WIN_SEARCH)   { icon = "?"; icon_col = VGA_LIGHT_CYAN; }

    gfx_fill_rounded_rect_rgb(win->x + 7, win->y + 5, 16, 15, 3,
                              gfx_rgb(14, 18, 26));
    gfx_rect_rounded(win->x + 7, win->y + 5, 16, 15, 3, icon_col);
    gfx_text(win->x + 11, win->y + 9, icon, icon_col, 0xFF);

    /* Kapat [X] */
    int close_x = win->x + win->w - 20;
    int close_y = win->y + 5;
    int close_hover = (mx >= close_x && mx <= close_x + 14 &&
                       my >= close_y && my <= close_y + 14);
    gfx_fill_rounded_rect_rgb(close_x, close_y, 14, 14, 3,
                              close_hover ? gfx_rgb(235, 72, 78)
                                          : gfx_rgb(176, 48, 54));
    if (close_hover) {
        gfx_rect_rounded_rgb(close_x, close_y, 14, 14, 3,
                             gfx_rgb(255, 145, 150));
    }
    gfx_char(close_x + 3, close_y + 3, 'x', VGA_WHITE, 0xFF);

    /* Kucult [-] */
    int min_x = win->x + win->w - 38;
    int min_y = win->y + 5;
    int min_hover = (mx >= min_x && mx <= min_x + 14 &&
                     my >= min_y && my <= min_y + 14);
    gfx_fill_rounded_rect_rgb(min_x, min_y, 14, 14, 3,
                              min_hover ? gfx_rgb(30, 126, 186)
                                        : gfx_rgb(33, 40, 54));
    gfx_rect_rounded_rgb(min_x, min_y, 14, 14, 3,
                         min_hover ? gfx_rgb(100, 210, 255)
                                   : gfx_rgb(54, 65, 86));
    gfx_hline(min_x + 3, min_y + 7, 8, VGA_WHITE);

    /* Buyut / Geri yukle [ ] */
    int max_x = win->x + win->w - 56;
    int max_y = win->y + 5;
    int max_hover = (mx >= max_x && mx <= max_x + 14 &&
                     my >= max_y && my <= max_y + 14);
    gfx_fill_rounded_rect_rgb(max_x, max_y, 14, 14, 3,
                              max_hover ? gfx_rgb(30, 126, 186)
                                        : gfx_rgb(33, 40, 54));
    gfx_rect_rounded_rgb(max_x, max_y, 14, 14, 3,
                         max_hover ? gfx_rgb(100, 210, 255)
                                   : gfx_rgb(54, 65, 86));
    if (win->is_maximized) {
        gfx_rect(max_x + 3, max_y + 4, 6, 6, VGA_WHITE);
        gfx_rect(max_x + 5, max_y + 2, 6, 6, VGA_WHITE);
    } else {
        gfx_rect(max_x + 4, max_y + 4, 7, 7, VGA_WHITE);
    }
}

/* ========================================================================= */
/* Pencere Icerik Cizimleri                                                  */
/* ========================================================================= */

/* 1. Terminal */
static void draw_terminal_content(window_t *win)
{
    int term_x = win->x + 6;
    int term_y = win->y + 26;
    int term_w = win->w - 12;
    int term_h = win->h - 32;

    gfx_fill_rect(term_x, term_y, term_w, term_h, GFX_BLACK);

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = term_chars[r][c];
            if (ch != ' ') {
                gfx_char(term_x + c * 8, term_y + r * 8,
                         (unsigned char)ch,
                         term_fg[r][c], GFX_BLACK);
            }
        }
    }

    if (win->is_active && term_row >= 0 && term_row < TERM_ROWS &&
        term_col >= 0 && term_col < TERM_COLS) {
        gfx_fill_rect(term_x + term_col * 8,
                      term_y + term_row * 8 + 6,
                      7, 2, VGA_LIGHT_GREEN);
    }
}

/* 2. Sistem Bilgisi */
static void draw_sysinfo_content(window_t *win)
{
    int sx = win->x + 12;
    int sy = win->y + 32;
    int sw = win->w - 24;

    gfx_fill_rounded_rect(sx, sy, sw, 64, 3, UI_CARD_BG);
    gfx_rect_rounded(sx, sy, sw, 64, 3, UI_BORDER);
    gfx_text(sx + 10, sy + 8,  "casrev-os v0.9 Modern Desktop", UI_ACCENT, 0xFF);
    gfx_text(sx + 10, sy + 24, "Mimari : x86 32-bit Korumali Mod", VGA_LIGHT_GREY, 0xFF);
    gfx_text(sx + 10, sy + 40, "Grafik : VESA 0x114 (800x600x16)", VGA_LIGHT_GREY, 0xFF);

    sy += 72;
    gfx_fill_rounded_rect(sx, sy, sw, 60, 3, UI_CARD_BG);
    gfx_rect_rounded(sx, sy, sw, 60, 3, UI_BORDER);

    char up_str[48];
    uint32_t ticks = timer_ticks();
    uint32_t sec = ticks / TIMER_HZ;
    uint32_t dsec = (ticks % TIMER_HZ) / 10;
    string_format(up_str, sizeof(up_str), "Uptime : %u.%u sn (Tick: %u | PIT: 100 Hz)", sec, dsec, ticks);
    gfx_text(sx + 10, sy + 8, up_str, VGA_LIGHT_CYAN, 0xFF);

    char date_str[16];
    char time_str[16];
    rtc_format_date(date_str);
    rtc_format_time(time_str);
    char dt_str[48];
    string_format(dt_str, sizeof(dt_str), "CMOS   : %s  %s", date_str, time_str);
    gfx_text(sx + 10, sy + 24, dt_str, VGA_LIGHT_GREY, 0xFF);
    gfx_text(sx + 10, sy + 40, "Giris  : PS/2 Klavye (IRQ1), PS/2 Fare (IRQ12)", VGA_LIGHT_GREY, 0xFF);

    sy += 68;
    gfx_fill_rounded_rect(sx, sy, sw, 80, 3, UI_CARD_BG);
    gfx_rect_rounded(sx, sy, sw, 80, 3, UI_BORDER);
    gfx_text(sx + 10, sy + 8, "Fiziksel Bellek (PMM & Heap):", UI_ACCENT, 0xFF);

    uint32_t tot_f = pmm_total_frames();
    uint32_t used_f = pmm_used_frames();
    uint32_t free_f = pmm_free_frames();
    uint32_t tot_mb = (uint32_t)(pmm_total_bytes() / (1024 * 1024));
    uint32_t used_mb = (used_f * 4096) / (1024 * 1024);

    char mem_info[64];
    string_format(mem_info, sizeof(mem_info), "Kullanilan: %u MB / %u MB (Bos: %u kare)", used_mb, tot_mb, free_f);
    gfx_text(sx + 10, sy + 24, mem_info, VGA_WHITE, 0xFF);

    int bar_x = sx + 10;
    int bar_y = sy + 42;
    int bar_w = sw - 20;
    int bar_h = 14;
    gfx_fill_rounded_rect(bar_x, bar_y, bar_w, bar_h, 2, GFX_BLACK);
    gfx_rect_rounded(bar_x, bar_y, bar_w, bar_h, 2, UI_BORDER);

    int fill_w = 0;
    if (tot_f > 0) {
        fill_w = (int)((used_f * (uint32_t)(bar_w - 2)) / tot_f);
    }
    if (fill_w < 4) fill_w = 4;
    if (fill_w > bar_w - 2) fill_w = bar_w - 2;
    gfx_fill_rounded_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 1, UI_PROGRESS);

    char heap_info[64];
    string_format(heap_info, sizeof(heap_info), "Heap: %u/%u B  Sayfalama: ACIK (32 MB)",
                  heap_used_bytes(), heap_total_bytes());
    gfx_text(sx + 10, sy + 62, heap_info, VGA_DARK_GREY, 0xFF);
}

/* 3. Hesap Makinesi */
static void draw_calc_content(window_t *win)
{
    int lcd_x = win->x + 14;
    int lcd_y = win->y + 32;
    int lcd_w = win->w - 28;
    int lcd_h = 36;
    gfx_fill_rounded_rect(lcd_x, lcd_y, lcd_w, lcd_h, 3, GFX_BLACK);
    gfx_rect_rounded(lcd_x, lcd_y, lcd_w, lcd_h, 3, UI_BORDER);

    uint32_t len = strlen(calc_display);
    int text_x = lcd_x + lcd_w - 10 - (int)(len * 8);
    if (text_x < lcd_x + 6) text_x = lcd_x + 6;
    gfx_text(text_x, lcd_y + 14, calc_display, VGA_LIGHT_GREEN, 0xFF);

    static const char *keys[5][4] = {
        { "C", "(", ")", "/" },
        { "7", "8", "9", "*" },
        { "4", "5", "6", "-" },
        { "1", "2", "3", "+" },
        { "0", "0", ".", "=" }
    };

    int start_x = win->x + 14;
    int start_y = win->y + 76;
    int btn_w = 48;
    int btn_h = 34;
    int gap_x = 6;
    int gap_y = 6;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            if (r == 4 && c == 1) continue;

            int bx = start_x + c * (btn_w + gap_x);
            int by = start_y + r * (btn_h + gap_y);
            int bw = (r == 4 && c == 0) ? (btn_w * 2 + gap_x) : btn_w;

            uint8_t btn_bg = UI_CARD_BG;
            uint8_t btn_fg = VGA_WHITE;

            const char *label = keys[r][c];
            if (label[0] == '=') btn_bg = UI_ACCENT;
            else if (label[0] == 'C') btn_bg = UI_CLOSE_RED;
            else if (label[0] == '+' || label[0] == '-' || label[0] == '*' || label[0] == '/') btn_fg = VGA_LIGHT_CYAN;

            gfx_fill_rounded_rect(bx, by, bw, btn_h, 3, btn_bg);
            gfx_rect_rounded(bx, by, bw, btn_h, 3, UI_BORDER);

            int tx = bx + (bw - (int)strlen(label) * 8) / 2;
            int ty = by + 13;
            gfx_text(tx, ty, label, btn_fg, 0xFF);
        }
    }
}

/* 4. Dosya Gezgini (File Explorer) */
static void draw_explorer_content(window_t *win)
{
    if (!explorer_dir) explorer_dir = vfs_get_root();

    int ex = win->x + 10;
    int ey = win->y + 30;
    int ew = win->w - 20;

    /* Konum & Geri Butonu */
    gfx_fill_rounded_rect(ex, ey, 60, 24, 2, UI_CARD_BG);
    gfx_rect_rounded(ex, ey, 60, 24, 2, UI_BORDER);
    gfx_text(ex + 8, ey + 8, "< Geri", VGA_LIGHT_CYAN, 0xFF);

    char pbuf[64];
    vfs_get_path(explorer_dir, pbuf, sizeof(pbuf));
    char path_label[80];
    string_format(path_label, sizeof(path_label), "Konum: %s", pbuf);
    gfx_fill_rounded_rect(ex + 68, ey, ew - 156, 24, 2, GFX_BLACK);
    gfx_rect_rounded(ex + 68, ey, ew - 156, 24, 2, UI_BORDER);
    gfx_text(ex + 74, ey + 8, path_label, VGA_WHITE, 0xFF);

    /* Yeni Metin Butonu */
    gfx_fill_rounded_rect(ex + ew - 82, ey, 82, 24, 2, UI_ACCENT);
    gfx_text(ex + ew - 76, ey + 8, "+ Yeni Not", VGA_WHITE, 0xFF);

    /* Dosya Listesi Kutusu */
    int list_y = ey + 32;
    int list_h = win->h - 72;
    gfx_fill_rounded_rect(ex, list_y, ew, list_h, 2, GFX_BLACK);
    gfx_rect_rounded(ex, list_y, ew, list_h, 2, UI_BORDER);

    vfs_node_t *cur = explorer_dir->first_child;
    int row_y = list_y + 8;

    if (!cur) {
        gfx_text(ex + 16, row_y, "(Dizin bos)", VGA_DARK_GREY, 0xFF);
        return;
    }

    while (cur && row_y + 20 < list_y + list_h) {
        if (cur->type == VFS_TYPE_DIR) {
            gfx_fill_rounded_rect(ex + 10, row_y - 2, 22, 16, 2, UI_CARD_BG);
            gfx_text(ex + 12, row_y + 2, "[]", VGA_YELLOW, 0xFF);
            gfx_text(ex + 40, row_y + 2, cur->name, VGA_YELLOW, 0xFF);
            gfx_text(ex + 200, row_y + 2, "<Klasor>", VGA_DARK_GREY, 0xFF);
        } else {
            gfx_fill_rounded_rect(ex + 10, row_y - 2, 22, 16, 2, UI_CARD_BG);
            gfx_text(ex + 14, row_y + 2, "F", VGA_LIGHT_CYAN, 0xFF);
            gfx_text(ex + 40, row_y + 2, cur->name, VGA_WHITE, 0xFF);

            char sz_str[24];
            string_format(sz_str, sizeof(sz_str), "%u B", cur->size);
            gfx_text(ex + 200, row_y + 2, sz_str, VGA_LIGHT_GREY, 0xFF);
            gfx_text(ex + 300, row_y + 2, "[Not Defterinde Ac]", UI_ACCENT, 0xFF);
        }
        row_y += 24;
        cur = cur->next_sibling;
    }
}

/* 5. Not Defteri (Notepad) */
static void draw_notepad_content(window_t *win)
{
    int nx = win->x + 10;
    int ny = win->y + 30;
    int nw = win->w - 20;

    /* Toolbar: Dosya adi ve Butonlar */
    char file_badge[48];
    string_format(file_badge, sizeof(file_badge), "Dosya: %s", notepad_filename);
    gfx_fill_rounded_rect(nx, ny, 160, 24, 2, UI_CARD_BG);
    gfx_text(nx + 8, ny + 8, file_badge, VGA_LIGHT_CYAN, 0xFF);

    /* Kaydet Butonu */
    gfx_fill_rounded_rect(nx + 168, ny, 70, 24, 2, UI_ACCENT);
    gfx_text(nx + 176, ny + 8, "Kaydet", VGA_WHITE, 0xFF);

    /* Temizle Butonu */
    gfx_fill_rounded_rect(nx + 244, ny, 74, 24, 2, UI_CARD_BG);
    gfx_text(nx + 252, ny + 8, "Temizle", VGA_LIGHT_RED, 0xFF);

    /* Metin Alani */
    int text_y = ny + 30;
    int text_h = win->h - 70;
    gfx_fill_rounded_rect(nx, text_y, nw, text_h, 2, GFX_BLACK);
    gfx_rect_rounded(nx, text_y, nw, text_h, 2, UI_BORDER);

    int draw_x = nx + 8;
    int draw_y = text_y + 8;
    int max_draw_y = text_y + text_h - 12;

    for (uint32_t i = 0; i < notepad_len && draw_y < max_draw_y; i++) {
        char ch = notepad_buf[i];

        /* Imlec */
        if (win->is_active && i == notepad_cursor) {
            gfx_fill_rect(draw_x, draw_y, 2, 8, VGA_LIGHT_GREEN);
        }

        if (ch == '\n') {
            draw_x = nx + 8;
            draw_y += 12;
        } else {
            gfx_char(draw_x, draw_y, (unsigned char)ch, VGA_WHITE, GFX_BLACK);
            draw_x += 8;
            if (draw_x + 12 > nx + nw) {
                draw_x = nx + 8;
                draw_y += 12;
            }
        }
    }

    if (win->is_active && notepad_cursor == notepad_len && draw_y < max_draw_y) {
        gfx_fill_rect(draw_x, draw_y, 2, 8, VGA_LIGHT_GREEN);
    }
}

/* 6. Paint / Cizim Tahtasi */
static void draw_paint_content(window_t *win)
{
    int px = win->x + 10;
    int py = win->y + 28;
    int pw = win->w - 20;

    /* Renk Paleti (8 Renk) */
    static const uint8_t colors[8] = {
        GFX_WHITE, GFX_BLACK, GFX_RED, GFX_GREEN,
        GFX_BLUE, GFX_YELLOW, GFX_CYAN, GFX_MAGENTA
    };

    for (int i = 0; i < 8; i++) {
        int cx = px + i * 22;
        gfx_fill_rounded_rect(cx, py, 18, 18, 2, colors[i]);
        if (!paint_is_eraser && paint_color == colors[i]) {
            gfx_rect_rounded(cx - 1, py - 1, 20, 20, 2, VGA_WHITE);
        }
    }

    /* Arac Butonlari */
    int tool_x = px + 184;

    /* Kalem */
    uint8_t pen_bg = (!paint_is_eraser) ? UI_ACCENT : UI_CARD_BG;
    gfx_fill_rounded_rect(tool_x, py, 46, 20, 2, pen_bg);
    gfx_text(tool_x + 6, py + 6, "Kalem", VGA_WHITE, 0xFF);

    /* Silgi */
    uint8_t era_bg = paint_is_eraser ? UI_ACCENT : UI_CARD_BG;
    gfx_fill_rounded_rect(tool_x + 50, py, 46, 20, 2, era_bg);
    gfx_text(tool_x + 56, py + 6, "Silgi", VGA_WHITE, 0xFF);

    /* Firca Boyutlari: 1, 3, 5 */
    gfx_fill_rounded_rect(tool_x + 102, py, 22, 20, 2, (paint_brush == 1) ? UI_ACCENT : UI_CARD_BG);
    gfx_text(tool_x + 108, py + 6, "1", VGA_WHITE, 0xFF);
    gfx_fill_rounded_rect(tool_x + 128, py, 22, 20, 2, (paint_brush == 2) ? UI_ACCENT : UI_CARD_BG);
    gfx_text(tool_x + 134, py + 6, "3", VGA_WHITE, 0xFF);
    gfx_fill_rounded_rect(tool_x + 154, py, 22, 20, 2, (paint_brush == 3) ? UI_ACCENT : UI_CARD_BG);
    gfx_text(tool_x + 160, py + 6, "5", VGA_WHITE, 0xFF);

    /* Temizle */
    gfx_fill_rounded_rect(px + pw - 48, py, 48, 20, 2, UI_CLOSE_RED);
    gfx_text(px + pw - 44, py + 6, "Temiz", VGA_WHITE, 0xFF);

    /* Cizim Tuvali */
    int canvas_y = py + 26;
    int canvas_w = pw;
    int canvas_h = win->h - 60;
    if (canvas_w > PAINT_W) canvas_w = PAINT_W;
    if (canvas_h > PAINT_H) canvas_h = PAINT_H;

    gfx_rect_rounded(px - 1, canvas_y - 1, canvas_w + 2, canvas_h + 2, 2, UI_BORDER);

    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
            gfx_pixel(px + x, canvas_y + y, paint_canvas[y * PAINT_W + x]);
        }
    }
}

/* 7. Mayin Tarlasi (Minesweeper) */
static void draw_mines_content(window_t *win)
{
    if (!mines_init_done) mines_init_game();

    int mx = win->x + 16;
    int my = win->y + 32;

    /* Ust Durum Paneli */
    gfx_fill_rounded_rect(mx, my, 208, 32, 2, GFX_BLACK);
    gfx_rect_rounded(mx, my, 208, 32, 2, UI_BORDER);

    /* Kalan Mayin Sayaci */
    char rem_str[8];
    string_format(rem_str, sizeof(rem_str), "%03d", mines_remaining);
    gfx_text(mx + 10, my + 12, rem_str, VGA_LIGHT_RED, 0xFF);

    /* Gulen Yuz / Reset Butonu */
    int face_x = mx + 92;
    int face_y = my + 6;
    gfx_fill_rounded_rect(face_x, face_y, 24, 20, 2, UI_CARD_BG);
    gfx_rect_rounded(face_x, face_y, 24, 20, 2, UI_BORDER);

    const char *face = ":)";
    if (mines_status == 1) face = "B)";
    else if (mines_status == 2) face = "X(";
    gfx_text(face_x + 5, face_y + 6, face, VGA_YELLOW, 0xFF);

    /* Zamanlayici */
    char tm_str[8];
    uint32_t sec = timer_seconds() % 1000;
    string_format(tm_str, sizeof(tm_str), "%03u", sec);
    gfx_text(mx + 168, my + 12, tm_str, VGA_LIGHT_RED, 0xFF);

    /* 9x9 Izgara */
    int grid_y = my + 40;
    int cell_size = 22;

    for (int r = 0; r < MINES_ROWS; r++) {
        for (int c = 0; c < MINES_COLS; c++) {
            int cx = mx + c * cell_size + 4;
            int cy = grid_y + r * cell_size;

            int st = mines_state[r][c];

            if (st == 1) {
                /* Acik Hucre */
                gfx_fill_rect(cx, cy, cell_size - 1, cell_size - 1, UI_CARD_BG);
                int val = mines_map[r][c];
                if (val == -1) {
                    /* Mayin */
                    gfx_fill_rect(cx, cy, cell_size - 1, cell_size - 1, UI_CLOSE_RED);
                    gfx_char(cx + 6, cy + 6, '*', VGA_WHITE, 0xFF);
                } else if (val > 0) {
                    static const uint8_t num_cols[9] = {
                        VGA_WHITE, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_RED,
                        VGA_LIGHT_MAGENTA, VGA_BROWN, VGA_LIGHT_CYAN, VGA_BLACK, VGA_DARK_GREY
                    };
                    gfx_char(cx + 7, cy + 6, (char)('0' + val), num_cols[val], 0xFF);
                }
            } else {
                /* Kapali Hucre */
                gfx_fill_rect(cx, cy, cell_size - 1, cell_size - 1, UI_WINDOW);
                gfx_hline(cx, cy, cell_size - 1, UI_BORDER);
                gfx_vline(cx, cy, cell_size - 1, UI_BORDER);

                if (st == 2) {
                    /* Bayrak */
                    gfx_char(cx + 7, cy + 6, 'P', VGA_YELLOW, 0xFF);
                }
            }
        }
    }

    if (mines_status == 1) {
        gfx_text(mx + 56, grid_y + 204, "TEBRIKLER!", VGA_LIGHT_GREEN, 0xFF);
    } else if (mines_status == 2) {
        gfx_text(mx + 66, grid_y + 204, "PATLADI!", VGA_LIGHT_RED, 0xFF);
    }
}

/* ========================================================================= */
/* Masaustu ve Gorev Cubugu Cizimi                                           */
/* ========================================================================= */
/* ========================================================================= */
/* Masaustu Duvar Kagidi                                                      */
/* ========================================================================= */
#define DESKTOP_H 562

static void wallpaper_rgb(int mode, int t, int *r, int *g, int *b)
{
    /* t: 0 (ust) .. 100 (alt); 3 duracli tam renk gecisi. */
    static const uint8_t stops[4][3][3] = {
        { {   6,   9,  20 }, {  18,  26,  48 }, {  54,  42,  72 } }, /* Gece */
        { {   3,  15,  30 }, {   8,  56,  82 }, {   0, 104, 132 } }, /* Okyanus */
        { {  44,  18,  52 }, { 126,  44,  72 }, { 224, 112,  52 } }, /* Gun Batimi */
        { {  10,  13,  22 }, {  10,  13,  22 }, {  10,  13,  22 } }  /* Klasik */
    };

    if (mode < 0 || mode > 3) mode = 0;
    if (t < 0) t = 0;
    if (t > 100) t = 100;

    int a;
    int c;
    int tt;
    if (t < 50) {
        a = 0;
        c = 1;
        tt = t * 2;
    } else {
        a = 1;
        c = 2;
        tt = (t - 50) * 2;
    }

    *r = stops[mode][a][0] + (stops[mode][c][0] - stops[mode][a][0]) * tt / 100;
    *g = stops[mode][a][1] + (stops[mode][c][1] - stops[mode][a][1]) * tt / 100;
    *b = stops[mode][a][2] + (stops[mode][c][2] - stops[mode][a][2]) * tt / 100;
}

static void draw_wallpaper_rect(int x, int y, int w, int h, int mode)
{
    if (w <= 0 || h <= 0) return;
    if (mode < 0 || mode > 3) mode = 0;

    if (mode == 3) {
        gfx_fill_rect_rgb(x, y, w, h, gfx_rgb(10, 13, 22));
        return;
    }

    int r0, g0, b0;
    int r1, g1, b1;
    int r2, g2, b2;
    wallpaper_rgb(mode, 0, &r0, &g0, &b0);
    wallpaper_rgb(mode, 50, &r1, &g1, &b1);
    wallpaper_rgb(mode, 100, &r2, &g2, &b2);

    int half = h / 2;
    if (half < 1) half = 1;

    gfx_gradient_v(x, y, w, half, gfx_rgb(r0, g0, b0), gfx_rgb(r1, g1, b1));
    gfx_gradient_v(x, y + half, w, h - half, gfx_rgb(r1, g1, b1),
                   gfx_rgb(r2, g2, b2));
}

static void draw_wallpaper(int mode)
{
    if (mode < 0 || mode > 3) mode = 0;

    draw_wallpaper_rect(0, 0, 800, DESKTOP_H, mode);

    if (mode != 3) {
        /* Ufuk isigi */
        uint16_t glow = (mode == 2) ? gfx_rgb(255, 130, 70) :
                        (mode == 1) ? gfx_rgb(0, 170, 200) :
                                      gfx_rgb(80, 105, 170);
        gfx_blend_rect(0, DESKTOP_H - 150, 800, 150, glow, 18);

        /* Yildizlar: parlaklik ve 4 kollu isilti cesitliligi. */
        static const uint16_t stars[][2] = {
            { 42, 54 }, { 96, 32 }, { 148, 78 }, { 212, 44 }, { 268, 96 },
            { 326, 38 }, { 382, 70 }, { 438, 28 }, { 492, 88 }, { 548, 46 },
            { 604, 74 }, { 660, 34 }, { 742, 58 }, { 780, 104 }, { 18, 120 },
            { 120, 140 }, { 190, 116 }, { 254, 154 }, { 340, 128 }, { 410, 160 },
            { 470, 118 }, { 536, 142 }, { 622, 112 }, { 690, 150 }, { 766, 136 }
        };
        for (uint32_t i = 0; i < sizeof(stars) / sizeof(stars[0]); i++) {
            int bright = 150 + (int)((i * 37) % 90);
            uint16_t c = gfx_rgb(bright, bright + 16, bright + 38);
            int sx = stars[i][0];
            int sy = stars[i][1];

            gfx_pixel_rgb(sx, sy, c);
            if ((i % 3) == 0) {
                gfx_pixel_rgb(sx + 1, sy, gfx_rgb(bright / 2, bright / 2 + 8,
                                                  bright / 2 + 20));
            }
            if ((i % 7) == 0) {
                gfx_pixel_rgb(sx - 1, sy, c);
                gfx_pixel_rgb(sx + 1, sy, c);
                gfx_pixel_rgb(sx, sy - 1, c);
                gfx_pixel_rgb(sx, sy + 1, c);
                gfx_pixel_rgb(sx, sy, gfx_rgb(245, 248, 255));
            }
        }

        /* Ay / gunes */
        if (mode == 2) {
            gfx_fill_circle_blend(690, 110, 58, gfx_rgb(255, 120, 50), 10);
            gfx_fill_circle_blend(690, 110, 48, gfx_rgb(255, 150, 60), 18);
            gfx_fill_circle_blend(690, 110, 40, gfx_rgb(255, 180, 80), 28);
            gfx_fill_circle_rgb(690, 110, 34, gfx_rgb(255, 175, 80));
            gfx_fill_circle_rgb(690, 110, 27, gfx_rgb(255, 218, 132));
        } else {
            gfx_fill_circle_blend(690, 96, 54, gfx_rgb(145, 180, 255), 9);
            gfx_fill_circle_blend(690, 96, 43, gfx_rgb(180, 205, 255), 15);
            gfx_fill_circle_rgb(690, 96, 30, gfx_rgb(205, 218, 242));
            gfx_fill_circle_rgb(690, 96, 27, gfx_rgb(242, 246, 255));

            int sr, sg, sb;
            wallpaper_rgb(mode, (96 * 100) / DESKTOP_H, &sr, &sg, &sb);
            gfx_fill_circle_rgb(700, 88, 22, gfx_rgb(sr, sg, sb));

            gfx_fill_circle_rgb(680, 88, 3, gfx_rgb(214, 224, 244));
            gfx_fill_circle_rgb(676, 104, 2, gfx_rgb(220, 229, 247));
        }
    }

    /* Sehir silueti */
    if (mode != 3) {
        static const uint8_t bld_w[18] = {
            38, 26, 48, 34, 58, 30, 44, 36, 52, 28, 46, 40, 32, 50, 36, 42, 30, 56
        };
        static const uint8_t bld_h[18] = {
            72, 118, 56, 140, 92, 164, 68, 108, 84, 148, 100, 64, 128, 88, 156, 76,
            112, 96
        };

        uint16_t bld_col = (mode == 2) ? gfx_rgb(20, 12, 22)
                                       : gfx_rgb(8, 11, 18);
        uint16_t bld_top = (mode == 2) ? gfx_rgb(52, 28, 38)
                                       : gfx_rgb(28, 37, 56);

        int x = 0;
        int i = 0;
        while (x < 800) {
            int w = bld_w[i % 18];
            int h = bld_h[i % 18];
            if (x + w > 800) w = 800 - x;
            int y = DESKTOP_H - h;

            gfx_fill_rect_rgb(x, y, w, h, bld_col);
            gfx_hline_rgb(x, y, w, bld_top);

            /* Bazi binalara anten ve kirmizi havacilik isigi. */
            if ((i % 5) == 2 && w > 20) {
                int ax = x + w / 2;
                gfx_vline_rgb(ax, y - 10, 10, bld_top);
                gfx_pixel_rgb(ax, y - 11, gfx_rgb(255, 90, 80));
            }

            for (int wy = y + 12; wy < DESKTOP_H - 8; wy += 14) {
                for (int wx = x + 6; wx < x + w - 5; wx += 12) {
                    if (((wx * 3 + wy * 5 + i * 7) % 13) < 3) {
                        uint16_t wc = ((wx + wy + i) % 11 == 0)
                                          ? gfx_rgb(100, 215, 230)
                                          : gfx_rgb(235, 185, 95);
                        gfx_fill_rect_rgb(wx, wy, 2, 3, wc);
                    }
                }
            }

            x += w;
            i++;
        }
    }

    /* Marka yazisi */
    gfx_text(349, 233, "casrev-os", GFX_BLACK, 0xFF);
    gfx_text(348, 232, "casrev-os", UI_BORDER, 0xFF);
    gfx_text(317, 249, "modern desktop environment", GFX_BLACK, 0xFF);
    gfx_text(316, 248, "modern desktop environment", UI_BORDER, 0xFF);
}

static void draw_settings_content(window_t *win)
{
    int sx = win->x + 12;
    int sy = win->y + 32;
    int sw = win->w - 24;
    int wide = (win->w >= 600);

    gfx_text(sx, sy, "Gorunum ve Tercihler", UI_ACCENT, 0xFF);
    gfx_text(sx, sy + 12, "Degisiklikler aninda uygulanir.", VGA_DARK_GREY, 0xFF);

    /* Duvar kagidi secenekleri */
    int card_w = (sw - 3 * 8) / 4;
    int card_h = wide ? 84 : 48;
    int card_y = sy + 30;

    gfx_text(sx, card_y, "Duvar Kagidi", VGA_WHITE, 0xFF);

    for (int i = 0; i < 4; i++) {
        int cx = sx + i * (card_w + 8);
        int cy = card_y + 14;

        draw_wallpaper_rect(cx, cy, card_w, card_h, i);
        gfx_rect_rounded(cx - 1, cy - 1, card_w + 2, card_h + 2, 3,
                         (wallpaper_mode == i) ? UI_ACCENT : UI_BORDER);
        if (wallpaper_mode == i) {
            gfx_rect_rounded(cx - 2, cy - 2, card_w + 4, card_h + 4, 4, UI_ACCENT_HOVER);
        }

        int name_len = (int)strlen(wallpaper_names[i]) * 8;
        int text_x = cx + (card_w - name_len) / 2;
        if (text_x < cx) text_x = cx;
        gfx_text(text_x, cy + card_h + 5, wallpaper_names[i],
                 (wallpaper_mode == i) ? UI_ACCENT : VGA_LIGHT_GREY, 0xFF);
    }

    int ky = card_y + 14 + card_h + 24;

    if (wide) {
        int gap = 12;
        int half = (sw - gap) / 2;
        int info_h = 84;

        /* Klavye bilgisi (sol) */
        gfx_fill_rounded_rect(sx, ky, half, info_h, 3, UI_CARD_BG);
        gfx_rect_rounded(sx, ky, half, info_h, 3, UI_BORDER);
        gfx_text(sx + 8, ky + 7, "Klavye: Turkce Q (KBDTUQ)", UI_ACCENT, 0xFF);
        gfx_text(sx + 8, ky + 23, "AltGr: @ # $ { } [ ] | \\ ~ < >", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, ky + 39, "Shift: ! ' ^ + % & / ( ) = ? _", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, ky + 55, "Ulusal harfler: 8x8 ozel glifler", VGA_DARK_GREY, 0xFF);

        /* Masaustu ve pencere bilgisi (sag) */
        int rx = sx + half + gap;
        gfx_fill_rounded_rect(rx, ky, half, info_h, 3, UI_CARD_BG);
        gfx_rect_rounded(rx, ky, half, info_h, 3, UI_BORDER);
        gfx_text(rx + 8, ky + 7, "Masaustu: 800x600, 65.536 renk", VGA_WHITE, 0xFF);
        gfx_text(rx + 8, ky + 23, "Pencere: surukle, [-] kucult", VGA_LIGHT_GREY, 0xFF);
        gfx_text(rx + 8, ky + 39, "Buyut/geri yukle: [ ] veya cift tik", VGA_LIGHT_GREY, 0xFF);
        gfx_text(rx + 8, ky + 55, "Kapat: [x]  |  Baslat menusu: ESC", VGA_LIGHT_GREY, 0xFF);

        /* Kisayollar */
        int ny = ky + info_h + 12;
        gfx_fill_rounded_rect(sx, ny, sw, 100, 3, UI_CARD_BG);
        gfx_rect_rounded(sx, ny, sw, 100, 3, UI_BORDER);
        gfx_text(sx + 8, ny + 7, "Kisayollar", UI_ACCENT, 0xFF);
        gfx_text(sx + 8, ny + 23, "ESC: Baslat menusu  |  Cift tik: buyut / geri yukle", VGA_WHITE, 0xFF);
        gfx_text(sx + 8, ny + 39, "Fare: pencereyi basligindan surukle  |  [-] kucult  |  [x] kapat", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, ny + 55, "Kabuk: help, ls, cat, settings  |  Dosyalar: RamFS", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, ny + 71, "Not: Yeniden baslatinca dosyalar sifirlanir.", VGA_DARK_GREY, 0xFF);

        /* Canli sistem durumu */
        int st_y = ny + 112;
        gfx_fill_rounded_rect(sx, st_y, sw, 112, 3, UI_CARD_BG);
        gfx_rect_rounded(sx, st_y, sw, 112, 3, UI_BORDER);
        gfx_text(sx + 8, st_y + 7, "Sistem Durumu", UI_ACCENT, 0xFF);

        uint32_t ticks = timer_ticks();
        uint32_t up_s = ticks / TIMER_HZ;
        uint32_t up_ds = (ticks % TIMER_HZ) / 10;
        char line[96];
        string_format(line, sizeof(line), "Uptime: %u.%u sn  |  Tick: %u  |  PIT: 100 Hz", up_s, up_ds, ticks);
        gfx_text(sx + 8, st_y + 23, line, VGA_LIGHT_CYAN, 0xFF);

        uint32_t tot_f = pmm_total_frames();
        uint32_t used_f = pmm_used_frames();
        uint32_t free_f = pmm_free_frames();
        uint32_t tot_mb = (uint32_t)(pmm_total_bytes() / (1024 * 1024));
        uint32_t used_mb = (used_f * 4096) / (1024 * 1024);
        string_format(line, sizeof(line), "RAM: %u/%u MB  |  Kullanilan kare: %u  |  Bos kare: %u",
                      used_mb, tot_mb, used_f, free_f);
        gfx_text(sx + 8, st_y + 39, line, VGA_LIGHT_GREY, 0xFF);

        int bar_x = sx + 8;
        int bar_y = st_y + 55;
        int bar_w = sw - 16;
        int bar_h = 12;
        gfx_fill_rounded_rect(bar_x, bar_y, bar_w, bar_h, 2, GFX_BLACK);
        gfx_rect_rounded(bar_x, bar_y, bar_w, bar_h, 2, UI_BORDER);
        int fill_w = 0;
        if (tot_f > 0) {
            fill_w = (int)((used_f * (uint32_t)(bar_w - 2)) / tot_f);
        }
        if (fill_w < 4) fill_w = 4;
        if (fill_w > bar_w - 2) fill_w = bar_w - 2;
        gfx_fill_rounded_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 1, UI_PROGRESS);

        char date_str[16];
        char time_str[16];
        rtc_format_date(date_str);
        rtc_format_time(time_str);
        string_format(line, sizeof(line), "Tarih: %s  |  Saat: %s  |  Cozunurluk: 800x600x16",
                      date_str, time_str);
        gfx_text(sx + 8, st_y + 75, line, VGA_LIGHT_GREY, 0xFF);

        int mx, my, buttons;
        mouse_get_state(&mx, &my, &buttons);
        string_format(line, sizeof(line), "Fare: %d,%d  |  Dugmeler: %d", mx, my, buttons);
        gfx_text(sx + 8, st_y + 91, line, VGA_DARK_GREY, 0xFF);
    } else {
        /* Klavye bilgisi */
        gfx_fill_rounded_rect(sx, ky, sw, 58, 3, UI_CARD_BG);
        gfx_rect_rounded(sx, ky, sw, 58, 3, UI_BORDER);
        gfx_text(sx + 8, ky + 7, "Klavye: Turkce Q (KBDTUQ)", UI_ACCENT, 0xFF);
        gfx_text(sx + 8, ky + 23, "AltGr: @ # $ { } [ ] | \\ ~ < >", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, ky + 39, "Shift: ! ' ^ + % & / ( ) = ? _", VGA_LIGHT_GREY, 0xFF);

        /* Sistem bilgisi */
        int iy = ky + 66;
        gfx_fill_rounded_rect(sx, iy, sw, 82, 3, UI_CARD_BG);
        gfx_rect_rounded(sx, iy, sw, 82, 3, UI_BORDER);
        gfx_text(sx + 8, iy + 7, "Masaustu: 800x600, 65.536 renk", VGA_WHITE, 0xFF);
        gfx_text(sx + 8, iy + 23, "Pencere: surukle, [-] kucult, [ ] buyut", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, iy + 39, "Kapat: [x]  |  Baslat: ESC veya dugme", VGA_LIGHT_GREY, 0xFF);
        gfx_text(sx + 8, iy + 55, "Surum: casrev-os v0.9  |  Ayarlar", VGA_LIGHT_GREY, 0xFF);
    }
}

static void search_draw_clipped(int x, int y, const char *text,
                                uint8_t fg, int max_chars)
{
    char buf[192];
    int len = text ? (int)strlen(text) : 0;
    int i = 0;

    if (max_chars > (int)sizeof(buf) - 1) max_chars = (int)sizeof(buf) - 1;
    if (len > max_chars) {
        int keep = max_chars - 3;
        if (keep < 0) keep = 0;
        while (i < keep) {
            buf[i] = text[i];
            i++;
        }
        buf[i++] = '.';
        buf[i++] = '.';
        buf[i++] = '.';
    } else {
        while (i < len) {
            buf[i] = text[i];
            i++;
        }
    }
    buf[i] = '\0';
    gfx_text(x, y, buf, fg, 0xFF);
}

/* Uzun metni kelime sinirlarina yaklasarak en fazla max_lines satira sigdirir. */
static void search_draw_wrapped(int x, int y, const char *text,
                                uint8_t fg, int max_chars, int max_lines)
{
    char buf[192];
    int len = text ? (int)strlen(text) : 0;
    int pos = 0;

    if (max_chars > (int)sizeof(buf) - 1) max_chars = (int)sizeof(buf) - 1;

    for (int line = 0; line < max_lines && pos < len; line++) {
        int remaining = len - pos;
        int capacity = max_chars;
        int last = (line == max_lines - 1);
        int take = remaining;

        if (last && remaining > capacity) {
            capacity = max_chars - 3;
            if (capacity < 1) capacity = 1;
            take = capacity;
        } else if (take > capacity) {
            take = capacity;
        }

        if (!last && pos + take < len) {
            int br = take;
            while (br > 0 && text[pos + br] != ' ') br--;
            if (br > max_chars / 2) take = br;
        }

        int n = 0;
        for (int i = 0; i < take && n < (int)sizeof(buf) - 4; i++) {
            buf[n++] = text[pos + i];
        }
        while (n > 0 && buf[n - 1] == ' ') n--;

        if (last && remaining > capacity) {
            buf[n++] = '.';
            buf[n++] = '.';
            buf[n++] = '.';
        }
        buf[n] = '\0';

        gfx_text(x, y + line * 12, buf, fg, 0xFF);

        pos += take;
        while (pos < len && text[pos] == ' ') pos++;
    }
}

static const char *search_net_status_text(void)
{
    if (!serial2_present()) return "Internet: yok";

    switch (search_net_state) {
    case SEARCH_NET_PENDING:
    case SEARCH_NET_WAITING:
        return "Internet: araniyor";
    case SEARCH_NET_READY:
        return (search_net_total > 0) ? "Internet: hazir"
                                      : "Internet: sonuc yok";
    case SEARCH_NET_ERROR:
        return "Internet: hata";
    case SEARCH_NET_IDLE:
        return "Internet: hazir";
    default:
        return "Internet: yok";
    }
}

static uint8_t search_net_status_color(void)
{
    if (!serial2_present()) return VGA_DARK_GREY;

    switch (search_net_state) {
    case SEARCH_NET_PENDING:
    case SEARCH_NET_WAITING:
        return VGA_YELLOW;
    case SEARCH_NET_READY:
        return (search_net_total > 0) ? VGA_LIGHT_GREEN : VGA_LIGHT_GREY;
    case SEARCH_NET_ERROR:
        return VGA_LIGHT_RED;
    case SEARCH_NET_IDLE:
        return VGA_LIGHT_GREEN;
    default:
        return VGA_DARK_GREY;
    }
}

static void draw_search_content(window_t *win)
{
    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);

    int x = win->x;
    int y = win->y;
    int field_x = x + SEARCH_PAD;
    int field_w = win->w - 2 * SEARCH_PAD - SEARCH_BTN_W - 8;
    int field_y = y + SEARCH_FIELD_TOP;
    int btn_x = field_x + field_w + 8;

    /* Google'a selam duran renkli yerel logo. */
    static const char *logo = "casrev";
    static const uint8_t logo_cols[6] = {
        UI_ACCENT, UI_CLOSE_RED, VGA_YELLOW,
        UI_ACCENT, UI_PROGRESS, UI_CLOSE_RED
    };
    int logo_x = x + (win->w - 6 * 8) / 2;
    int logo_y = y + 36;
    for (int i = 0; i < 6; i++) {
        gfx_char(logo_x + i * 8, logo_y, (unsigned char)logo[i],
                 logo_cols[i], 0xFF);
    }

    const char *subtitle = "Yerel + internet arama";
    int sub_x = x + (win->w - (int)strlen(subtitle) * 8) / 2;
    gfx_text(sub_x, y + 52, subtitle, VGA_DARK_GREY, 0xFF);

    /* Arama alani */
    gfx_fill_rounded_rect_rgb(field_x, field_y, field_w, SEARCH_FIELD_H, 6,
                              gfx_rgb(28, 34, 48));
    gfx_rect_rounded_rgb(field_x, field_y, field_w, SEARCH_FIELD_H, 6,
                         win->is_active ? gfx_rgb(0, 150, 240)
                                        : gfx_rgb(52, 62, 82));
    if (search_query[0]) {
        gfx_text(field_x + 12, field_y + 12, search_query, VGA_WHITE, 0xFF);
        if (win->is_active && ((timer_ticks() / 50) & 1)) {
            gfx_vline_rgb(field_x + 12 + search_query_len * 8,
                          field_y + 9, 14, gfx_rgb(95, 205, 255));
        }
    } else {
        gfx_text(field_x + 12, field_y + 12,
                 "Dosya, uygulama veya web ara...", VGA_DARK_GREY, 0xFF);
    }

    /* Ara butonu */
    int btn_hover = (mx >= btn_x && mx <= btn_x + SEARCH_BTN_W &&
                     my >= field_y && my <= field_y + SEARCH_FIELD_H);
    gfx_fill_rounded_rect_gradient_v(btn_x, field_y, SEARCH_BTN_W,
                                     SEARCH_FIELD_H, 5,
                                     btn_hover ? gfx_rgb(50, 175, 250)
                                               : gfx_rgb(32, 145, 225),
                                     btn_hover ? gfx_rgb(0, 100, 180)
                                               : gfx_rgb(0, 92, 165));
    gfx_rect_rounded_rgb(btn_x, field_y, SEARCH_BTN_W, SEARCH_FIELD_H, 5,
                         btn_hover ? gfx_rgb(130, 220, 255)
                                   : gfx_rgb(70, 150, 210));
    gfx_text(btn_x + 28, field_y + 12, "Ara", VGA_WHITE, 0xFF);

    const char *hint = search_query[0]
        ? "Enter: ilk sonucu ac | yazmaya devam edin"
        : "RamFS, uygulamalar ve web uzerinde arama";
    int hint_x = x + (win->w - (int)strlen(hint) * 8) / 2;
    if (hint_x < x + 20) hint_x = x + 20;
    search_draw_clipped(hint_x, y + 112, hint, VGA_DARK_GREY, 60);

    /* Sonuclar */
    int row_x = x + 20;
    int row_w = win->w - 40;

    if (search_detail >= 0 && search_detail < search_result_count) {
        search_result_t *r = &search_results[search_detail];
        int dx = row_x;
        int dy = y + SEARCH_RESULTS_TOP;
        int dw = row_w;
        int dh = 250;
        int text_chars = (dw - 32) / 8;

        gfx_fill_rounded_rect_rgb(dx, dy, dw, dh, 5, gfx_rgb(23, 28, 39));
        gfx_rect_rounded_rgb(dx, dy, dw, dh, 5, gfx_rgb(60, 130, 190));
        gfx_text(dx + 16, dy + 10, "WEB SONUCU", UI_ACCENT, 0xFF);

        int back_x = dx + dw - 68;
        int back_y = dy + 8;
        int back_hover = (mx >= back_x && mx <= back_x + 52 &&
                          my >= back_y && my <= back_y + 22);
        gfx_fill_rounded_rect_rgb(back_x, back_y, 52, 22, 3,
                                  back_hover ? gfx_rgb(45, 58, 82)
                                             : gfx_rgb(28, 35, 50));
        gfx_rect_rounded_rgb(back_x, back_y, 52, 22, 3,
                             back_hover ? gfx_rgb(95, 205, 255)
                                        : gfx_rgb(55, 70, 95));
        gfx_text(back_x + 11, back_y + 7, "Geri", VGA_WHITE, 0xFF);

        search_draw_clipped(dx + 16, dy + 32, r->title, VGA_WHITE, text_chars);
        search_draw_clipped(dx + 16, dy + 50, r->meta, UI_ACCENT, text_chars);

        gfx_text(dx + 16, dy + 74, "URL", VGA_DARK_GREY, 0xFF);
        search_draw_wrapped(dx + 16, dy + 86, r->path,
                            VGA_LIGHT_CYAN, text_chars, 2);

        gfx_text(dx + 16, dy + 122, "Ozet", VGA_DARK_GREY, 0xFF);
        search_draw_wrapped(dx + 16, dy + 134, r->detail,
                            VGA_LIGHT_GREY, text_chars, 3);

        gfx_hline_rgb(dx + 16, dy + 172, dw - 32, gfx_rgb(45, 55, 74));
        gfx_text(dx + 16, dy + 184, "DuckDuckGo uzerinden anonim arama",
                 VGA_DARK_GREY, 0xFF);
        gfx_text(dx + 16, dy + 200, "Sayfa icerigi bu surumde goruntulenmiyor.",
                 VGA_DARK_GREY, 0xFF);
    } else if (search_result_count == 0) {
        const char *none = "Sonuc bulunamadi";
        const char *tip = "Dosya adi, icerik, uygulama veya komut deneyin.";

        if (search_query[0] &&
            (search_net_state == SEARCH_NET_PENDING ||
             search_net_state == SEARCH_NET_WAITING)) {
            none = "Internet araniyor...";
            tip = "Yerel eslesme yok; web sonuclari bekleniyor.";
        } else if (search_query[0] && search_net_state == SEARCH_NET_ERROR) {
            none = "Internet baglantisi yok";
            tip = "Yerel sonuc yok. Yerel aramada kalabilirsiniz.";
        }

        int nx = x + (win->w - (int)strlen(none) * 8) / 2;
        gfx_text(nx, y + SEARCH_RESULTS_TOP + 52, none, UI_ACCENT, 0xFF);

        int tx = x + (win->w - (int)strlen(tip) * 8) / 2;
        if (tx < x + 20) tx = x + 20;
        search_draw_clipped(tx, y + SEARCH_RESULTS_TOP + 72, tip,
                            VGA_DARK_GREY, 58);
    } else {
        for (int i = 0; i < search_result_count; i++) {
            int row_y = y + SEARCH_RESULTS_TOP + i * SEARCH_ROW_H;
            int hover = (mx >= row_x && mx <= row_x + row_w &&
                         my >= row_y && my <= row_y + SEARCH_ROW_H - 4);
            search_result_t *r = &search_results[i];

            gfx_fill_rounded_rect_rgb(row_x, row_y, row_w, SEARCH_ROW_H - 4, 4,
                                      hover ? gfx_rgb(36, 46, 66)
                                            : gfx_rgb(23, 28, 39));
            gfx_rect_rounded_rgb(row_x, row_y, row_w, SEARCH_ROW_H - 4, 4,
                                 hover ? gfx_rgb(60, 130, 190)
                                       : gfx_rgb(43, 53, 72));

            int icon_x = row_x + 8;
            int icon_y = row_y + 6;
            gfx_fill_rounded_rect_rgb(icon_x, icon_y, 30, 30, 4,
                                      gfx_rgb(15, 19, 28));
            gfx_rect_rounded_rgb(icon_x, icon_y, 30, 30, 4, r->icon_color);
            int ilen = (int)strlen(r->icon);
            int itx = icon_x + (30 - ilen * 8) / 2;
            gfx_text(itx, icon_y + 11, r->icon, r->icon_color, 0xFF);

            int text_x = row_x + 48;
            search_draw_clipped(text_x, row_y + 5, r->title, VGA_WHITE, 42);
            search_draw_clipped(text_x, row_y + 17, r->meta, UI_ACCENT, 52);
            search_draw_clipped(text_x, row_y + 29, r->detail,
                                VGA_LIGHT_GREY, 52);
        }
    }

    /* Alt durum satiri */
    int local_shown = search_local_count;
    int net_shown = 0;
    if (search_net_state == SEARCH_NET_READY && search_net_count > 0) {
        if (local_shown > 3) local_shown = 3;
        net_shown = search_net_count;
        if (net_shown > SEARCH_MAX_RESULTS - local_shown) {
            net_shown = SEARCH_MAX_RESULTS - local_shown;
        }
    } else if (local_shown > SEARCH_MAX_RESULTS) {
        local_shown = SEARCH_MAX_RESULTS;
    }

    char status[80];
    if (!search_query[0]) {
        string_format(status, sizeof(status), "Hizli erisim | %d uygulama",
                      search_local_total);
    } else if (search_total == 0) {
        if (search_net_state == SEARCH_NET_PENDING ||
            search_net_state == SEARCH_NET_WAITING) {
            string_format(status, sizeof(status), "Yerel sonuc yok | web araniyor");
        } else if (search_net_state == SEARCH_NET_ERROR) {
            string_format(status, sizeof(status), "Sonuc bulunamadi | web yok");
        } else {
            string_format(status, sizeof(status), "Sonuc bulunamadi");
        }
    } else if (search_net_state == SEARCH_NET_READY && net_shown > 0) {
        if (search_total > search_result_count) {
            string_format(status, sizeof(status),
                          "%d sonuc | ilk %d: %d yerel + %d web",
                          search_total, search_result_count,
                          local_shown, net_shown);
        } else {
            string_format(status, sizeof(status), "%d sonuc | %d yerel + %d web",
                          search_total, local_shown, net_shown);
        }
    } else if (search_net_state == SEARCH_NET_PENDING ||
               search_net_state == SEARCH_NET_WAITING) {
        string_format(status, sizeof(status), "%d yerel eslesme | web araniyor",
                      search_local_total);
    } else if (search_net_state == SEARCH_NET_ERROR) {
        string_format(status, sizeof(status), "%d yerel eslesme | web yok",
                      search_local_total);
    } else if (search_total > search_result_count) {
        string_format(status, sizeof(status), "%d sonuc | ilk %d yerel",
                      search_total, search_result_count);
    } else {
        string_format(status, sizeof(status), "%d sonuc | yerel arama",
                      search_total);
    }
    search_draw_clipped(x + SEARCH_PAD, y + win->h - 20, status,
                        VGA_DARK_GREY, 48);

    const char *net_text = search_net_status_text();
    int net_x = x + win->w - SEARCH_PAD - (int)strlen(net_text) * 8;
    gfx_text(net_x, y + win->h - 20, net_text,
             search_net_status_color(), 0xFF);
}

static void draw_desktop_icons(void)
{
    static const char *labels[9] = {
        "Terminal", "Gezgin", "Notlar", "Paint",
        "Mayin", "Hesap", "Sistem", "Arama", "Ayarlar"
    };
    static const char *icons[9] = {
        ">_", "[]", "N", "P", "*", "#", "i", "?", "A"
    };
    static const uint8_t colors[9] = {
        UI_ACCENT, VGA_YELLOW, VGA_LIGHT_CYAN, VGA_LIGHT_RED,
        VGA_YELLOW, UI_ACCENT, UI_ACCENT, VGA_LIGHT_CYAN, VGA_LIGHT_GREEN
    };

    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);

    for (int i = 0; i < 9; i++) {
        int x = 18;
        int y = 12 + i * 60;
        int hover = (mx >= x && mx <= x + 48 && my >= y && my <= y + 60);

        uint16_t card_bg = hover ? gfx_rgb(38, 48, 68)
                                 : gfx_rgb(19, 23, 33);
        gfx_fill_rounded_rect_rgb(x, y, 48, 44, 5, card_bg);
        gfx_rect_rounded_rgb(x, y, 48, 44, 5,
                             hover ? gfx_rgb(0, 150, 240)
                                   : gfx_rgb(48, 57, 74));
        if (hover) {
            gfx_hline_rgb(x + 6, y + 1, 36, gfx_rgb(95, 205, 255));
        }

        if (i == 7) {
            /* Arama: cizilmis buyutec */
            gfx_fill_circle_rgb(x + 24, y + 20, 9, VGA_LIGHT_CYAN);
            gfx_fill_circle_rgb(x + 24, y + 20, 6, card_bg);
            gfx_fill_rect_rgb(x + 30, y + 27, 3, 3, VGA_LIGHT_CYAN);
            gfx_fill_rect_rgb(x + 32, y + 29, 3, 3, VGA_LIGHT_CYAN);
            gfx_fill_rect_rgb(x + 34, y + 31, 3, 3, VGA_LIGHT_CYAN);
        } else {
            draw_text_shadow(x + 16, y + 16, icons[i], colors[i]);
        }

        gfx_text(x + 3, y + 51, labels[i], GFX_BLACK, 0xFF);
        gfx_text(x + 2, y + 50, labels[i],
                 hover ? VGA_WHITE : VGA_LIGHT_GREY, 0xFF);
    }
}

static int taskbar_visible_count(void)
{
    int count = 0;
    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (windows[i].is_visible) count++;
    }
    return count;
}

static int taskbar_tab_width(void)
{
    int count = taskbar_visible_count();
    if (count <= 0) return 68;

    int width = (432 - (count - 1) * 4) / count;
    if (width > 68) width = 68;
    if (width < 44) width = 44;
    return width;
}

static void draw_taskbar(void)
{
    int ty = DESKTOP_H;
    int th = 38;

    /* Yari saydam koyu cam cubuk + ust isik cizgisi */
    gfx_blend_rect(0, ty, 800, th, gfx_rgb(10, 14, 22), 228);
    gfx_hline_rgb(0, ty, 800, gfx_rgb(62, 78, 108));
    gfx_hline_rgb(0, ty + 1, 800, gfx_rgb(28, 34, 46));

    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);

    /* Baslat Butonu */
    int btn_x = 8;
    int btn_y = ty + 4;
    int btn_w = 84;
    int btn_h = 30;
    int start_hover = (mx >= btn_x && mx <= btn_x + btn_w &&
                       my >= btn_y && my <= btn_y + btn_h);

    uint16_t start_top;
    uint16_t start_bottom;
    if (start_menu_open) {
        start_top = gfx_rgb(40, 166, 245);
        start_bottom = gfx_rgb(0, 100, 180);
    } else if (start_hover) {
        start_top = gfx_rgb(38, 62, 92);
        start_bottom = gfx_rgb(24, 39, 58);
    } else {
        start_top = gfx_rgb(33, 41, 56);
        start_bottom = gfx_rgb(20, 25, 36);
    }
    gfx_fill_rounded_rect_gradient_v(btn_x, btn_y, btn_w, btn_h, 4,
                                     start_top, start_bottom);
    gfx_rect_rounded_rgb(btn_x, btn_y, btn_w, btn_h, 4,
                         (start_hover || start_menu_open)
                             ? gfx_rgb(95, 205, 255)
                             : gfx_rgb(58, 70, 92));

    uint16_t logo_col = start_menu_open ? gfx_rgb(255, 255, 255)
                                        : gfx_rgb(110, 220, 255);
    gfx_fill_rect_rgb(btn_x + 8, btn_y + 8, 4, 4, logo_col);
    gfx_fill_rect_rgb(btn_x + 14, btn_y + 8, 4, 4, logo_col);
    gfx_fill_rect_rgb(btn_x + 8, btn_y + 14, 4, 4, logo_col);
    gfx_fill_rect_rgb(btn_x + 14, btn_y + 14, 4, 4, logo_col);
    gfx_text(btn_x + 24, btn_y + 11, "Baslat", VGA_WHITE, 0xFF);

    /* Pencere Sekmeleri */
    static const char *tab_titles[UI_WIN_COUNT] = {
        "Term", "Bilgi", "Hesap", "Dosya",
        "Not", "Paint", "Mayin", "Ayar", "Arama"
    };

    int tab_x = 100;
    int tab_w = taskbar_tab_width();
    int tab_h = btn_h;

    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (!windows[i].is_visible) continue;

        int hover = (mx >= tab_x && mx <= tab_x + tab_w &&
                     my >= btn_y && my <= btn_y + tab_h);

        uint16_t tab_bg;
        uint16_t tab_border;
        if (windows[i].is_active) {
            tab_bg = gfx_rgb(42, 52, 72);
            tab_border = gfx_rgb(0, 150, 240);
        } else if (hover) {
            tab_bg = gfx_rgb(30, 37, 50);
            tab_border = gfx_rgb(65, 80, 105);
        } else {
            tab_bg = gfx_rgb(15, 19, 28);
            tab_border = gfx_rgb(38, 46, 60);
        }

        gfx_fill_rounded_rect_rgb(tab_x, btn_y, tab_w, tab_h, 4, tab_bg);
        gfx_rect_rounded_rgb(tab_x, btn_y, tab_w, tab_h, 4, tab_border);

        if (windows[i].is_active) {
            gfx_hline_rgb(tab_x + 6, btn_y + 2, tab_w - 12,
                          gfx_rgb(95, 205, 255));
        }

        int title_len = (int)strlen(tab_titles[i]) * 8;
        int title_x = tab_x + (tab_w - title_len) / 2;
        if (title_x < tab_x + 2) title_x = tab_x + 2;
        gfx_text(title_x, btn_y + 11, tab_titles[i],
                 windows[i].is_minimized ? VGA_DARK_GREY
                                         : (windows[i].is_active ? VGA_WHITE
                                                                 : VGA_LIGHT_GREY),
                 0xFF);
        tab_x += tab_w + 4;
    }

    /* Sistem Tepsisi (Tray) */
    uint32_t tot_mb = (uint32_t)(pmm_total_bytes() / (1024 * 1024));
    uint32_t used_mb = (pmm_used_frames() * 4096) / (1024 * 1024);
    char ram_str[24];
    string_format(ram_str, sizeof(ram_str), "RAM:%u/%uM", used_mb, tot_mb);

    gfx_fill_rounded_rect_rgb(540, btn_y + 3, 90, 24, 3,
                              gfx_rgb(20, 25, 36));
    gfx_rect_rounded_rgb(540, btn_y + 3, 90, 24, 3,
                         gfx_rgb(42, 52, 70));
    gfx_text(546, btn_y + 11, ram_str, VGA_LIGHT_CYAN, 0xFF);

    gfx_fill_rounded_rect_rgb(636, btn_y + 3, 86, 24, 3,
                              gfx_rgb(20, 25, 36));
    gfx_rect_rounded_rgb(636, btn_y + 3, 86, 24, 3,
                         gfx_rgb(42, 52, 70));
    gfx_text(642, btn_y + 11, mouse_text, VGA_LIGHT_GREY, 0xFF);

    gfx_fill_rounded_rect_rgb(728, btn_y + 3, 66, 24, 3,
                              gfx_rgb(20, 25, 36));
    gfx_rect_rounded_rgb(728, btn_y + 3, 66, 24, 3,
                         gfx_rgb(42, 52, 70));
    gfx_text(730, btn_y + 11, clock_text, VGA_YELLOW, 0xFF);
}

static void draw_start_menu(void)
{
    if (!start_menu_open) return;

    int menu_x = 8;
    int menu_y = 240;
    int menu_w = 224;
    int menu_h = 322;

    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);

    gfx_shadow_soft(menu_x, menu_y, menu_w, menu_h);
    gfx_blend_rect(menu_x, menu_y, menu_w, menu_h,
                   gfx_rgb(14, 18, 27), 238);
    gfx_rect_rounded_rgb(menu_x, menu_y, menu_w, menu_h, 5,
                         gfx_rgb(62, 78, 108));

    gfx_fill_rounded_rect_gradient_v(menu_x, menu_y, menu_w, 30, 5,
                                     gfx_rgb(30, 41, 60),
                                     gfx_rgb(20, 26, 38));
    gfx_text(menu_x + 12, menu_y + 9, "casrev-os v0.9", UI_ACCENT, 0xFF);
    gfx_text(menu_x + 146, menu_y + 9, "root", VGA_LIGHT_GREY, 0xFF);
    gfx_hline_rgb(menu_x, menu_y + 30, menu_w, gfx_rgb(45, 55, 74));

    static const char *items[11] = {
        "?  Arama (Yerel)",
        ">_ Terminal (Kabuk)",
        "[] Dosya Gezgini",
        "N  Not Defteri",
        "P  Paint Cizim",
        "*  Mayin Tarlasi",
        "#  Hesap Makinesi",
        "i  Sistem Bilgisi",
        "A  Ayarlar",
        "R  Yeniden Baslat",
        "X  Sistemi Kapat"
    };

    int iy = menu_y + 36;
    for (int i = 0; i < 11; i++) {
        if (i == 9) {
            gfx_hline_rgb(menu_x + 10, iy, menu_w - 20,
                          gfx_rgb(48, 58, 78));
            iy += 6;
        }

        int row_y = iy;
        int hover = (mx >= menu_x + 6 && mx <= menu_x + menu_w - 6 &&
                     my >= row_y && my <= row_y + 22);

        gfx_fill_rounded_rect_rgb(menu_x + 6, row_y, menu_w - 12, 22, 3,
                                  hover ? gfx_rgb(40, 51, 72)
                                        : gfx_rgb(20, 24, 34));
        if (hover) {
            gfx_fill_rect_rgb(menu_x + 8, row_y + 6, 2, 10,
                              gfx_rgb(0, 150, 240));
            gfx_rect_rounded_rgb(menu_x + 6, row_y, menu_w - 12, 22, 3,
                                 gfx_rgb(60, 130, 190));
        }

        gfx_text(menu_x + 16, row_y + 6, items[i],
                 (i >= 9) ? VGA_LIGHT_RED : VGA_WHITE, 0xFF);
        iy += 24;
    }
}

/* ========================================================================= */
/* Ana Render Islemi                                                         */
/* ========================================================================= */
void ui_render(void)
{
    if (!gfx_ready()) {
        return;
    }

    /* 1. Masaustu */
    draw_wallpaper(wallpaper_mode);

    /* 2. Simgeler */
    draw_desktop_icons();

    /* 3. Pencereler */
    int active_idx = -1;
    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (!windows[i].is_visible || windows[i].is_minimized) continue;
        if (windows[i].is_active) {
            active_idx = i;
            continue;
        }

        draw_window_header(&windows[i]);
        if (windows[i].id == UI_WIN_TERMINAL) draw_terminal_content(&windows[i]);
        else if (windows[i].id == UI_WIN_SYSINFO) draw_sysinfo_content(&windows[i]);
        else if (windows[i].id == UI_WIN_CALC) draw_calc_content(&windows[i]);
        else if (windows[i].id == UI_WIN_EXPLORER) draw_explorer_content(&windows[i]);
        else if (windows[i].id == UI_WIN_NOTEPAD) draw_notepad_content(&windows[i]);
        else if (windows[i].id == UI_WIN_PAINT) draw_paint_content(&windows[i]);
        else if (windows[i].id == UI_WIN_MINES) draw_mines_content(&windows[i]);
        else if (windows[i].id == UI_WIN_SETTINGS) draw_settings_content(&windows[i]);
        else if (windows[i].id == UI_WIN_SEARCH) draw_search_content(&windows[i]);
    }

    if (active_idx >= 0) {
        draw_window_header(&windows[active_idx]);
        if (windows[active_idx].id == UI_WIN_TERMINAL) draw_terminal_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_SYSINFO) draw_sysinfo_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_CALC) draw_calc_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_EXPLORER) draw_explorer_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_NOTEPAD) draw_notepad_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_PAINT) draw_paint_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_MINES) draw_mines_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_SETTINGS) draw_settings_content(&windows[active_idx]);
        else if (windows[active_idx].id == UI_WIN_SEARCH) draw_search_content(&windows[active_idx]);
    }

    /* 4. Alt Gorev Cubugu */
    draw_taskbar();

    /* 5. Baslat Menusu */
    draw_start_menu();

    /* 6. Fare */
    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);
    draw_mouse_cursor(mx, my, buttons);

    gfx_flush();
}

/* ========================================================================= */
/* Fare Tiklama ve Etkilesim                                                 */
/* ========================================================================= */
static void handle_mouse_input(int mx, int my, int just_pressed, int just_released, int is_held)
{
    /* 1. Pencere Surukleme */
    if (is_held && drag_win >= 0) {
        windows[drag_win].x = mx - drag_off_x;
        windows[drag_win].y = my - drag_off_y;

        if (windows[drag_win].x < 0) windows[drag_win].x = 0;
        if (windows[drag_win].x > 800 - 80) windows[drag_win].x = 800 - 80;
        if (windows[drag_win].y < 0) windows[drag_win].y = 0;
        if (windows[drag_win].y > 560 - 30) windows[drag_win].y = 560 - 30;

        ui_dirty = 1;
        return;
    }

    /* Paint Uzerinde Cizim (Fare Basili Tutulurken) */
    if (is_held && windows[UI_WIN_PAINT].is_visible &&
        !windows[UI_WIN_PAINT].is_minimized &&
        windows[UI_WIN_PAINT].is_active) {
        window_t *pw = &windows[UI_WIN_PAINT];
        int px = pw->x + 10;
        int py = pw->y + 54;
        if (mx >= px && mx < px + PAINT_W && my >= py && my < py + PAINT_H) {
            paint_plot(mx - px, my - py);
            ui_dirty = 1;
            return;
        }
    }

    if (just_released) {
        drag_win = -1;
    }

    if (!just_pressed) {
        return;
    }

    /* 2. Baslat Menusu */
    if (start_menu_open) {
        int mx0 = 8, my0 = 240, mw0 = 224, mh0 = 322;
        if (mx >= mx0 && mx <= mx0 + mw0 && my >= my0 && my <= my0 + mh0) {
            int iy = my0 + 36;
            for (int i = 0; i < 11; i++) {
                if (i == 9) iy += 6;
                if (my >= iy && my <= iy + 22) {
                    if (i == 0) ui_window_open(UI_WIN_SEARCH);
                    else if (i == 1) ui_window_open(UI_WIN_TERMINAL);
                    else if (i == 2) ui_window_open(UI_WIN_EXPLORER);
                    else if (i == 3) ui_window_open(UI_WIN_NOTEPAD);
                    else if (i == 4) ui_window_open(UI_WIN_PAINT);
                    else if (i == 5) ui_window_open(UI_WIN_MINES);
                    else if (i == 6) ui_window_open(UI_WIN_CALC);
                    else if (i == 7) ui_window_open(UI_WIN_SYSINFO);
                    else if (i == 8) ui_window_open(UI_WIN_SETTINGS);
                    else if (i == 9) { outb(0x64, 0xFE); }
                    else if (i == 10) { for (;;) __asm__ volatile("cli; hlt"); }
                    start_menu_open = 0;
                    ui_dirty = 1;
                    return;
                }
                iy += 24;
            }
            return;
        } else {
            start_menu_open = 0;
            ui_dirty = 1;
        }
    }

    /* 3. Gorev Cubugu */
    if (my >= 562) {
        if (mx >= 8 && mx <= 92 && my >= 566 && my <= 596) {
            ui_start_menu_toggle();
            return;
        }

        int tab_x = 100;
        int tab_w = taskbar_tab_width();
        for (int i = 0; i < UI_WIN_COUNT; i++) {
            if (!windows[i].is_visible) continue;
            if (mx >= tab_x && mx <= tab_x + tab_w && my >= 566 && my <= 596) {
                ui_window_toggle(i);
                return;
            }
            tab_x += tab_w + 4;
        }
        return;
    }

    /* 4. Pencereler */
    int win_order[UI_WIN_COUNT];
    int count = 0;
    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (windows[i].is_visible && !windows[i].is_minimized && windows[i].is_active) {
            win_order[count++] = i;
            break;
        }
    }
    for (int i = 0; i < UI_WIN_COUNT; i++) {
        if (windows[i].is_visible && !windows[i].is_minimized && !windows[i].is_active) {
            win_order[count++] = i;
        }
    }

    for (int idx = 0; idx < count; idx++) {
        int i = win_order[idx];
        window_t *w = &windows[i];

        if (mx >= w->x && mx <= w->x + w->w && my >= w->y && my <= w->y + w->h) {
            set_active_window(i);

            /* Baslik Cubugu */
            if (my <= w->y + 24) {
                if (mx >= w->x + w->w - 20 && mx <= w->x + w->w - 6) {
                    ui_window_close(i);
                    return;
                }
                if (mx >= w->x + w->w - 38 && mx <= w->x + w->w - 24) {
                    w->is_minimized = 1;
                    w->is_active = 0;
                    activate_fallback_window(i);
                    ui_dirty = 1;
                    return;
                }
                if (mx >= w->x + w->w - 56 && mx <= w->x + w->w - 42) {
                    toggle_maximize(i);
                    return;
                }

                uint32_t now = timer_ticks();
                if (last_title_click_win == i &&
                    (now - last_title_click_ticks) < 40) {
                    toggle_maximize(i);
                    last_title_click_win = -1;
                    return;
                }
                last_title_click_win = i;
                last_title_click_ticks = now;

                if (w->is_maximized) {
                    ui_dirty = 1;
                    return;
                }

                drag_win = i;
                drag_off_x = mx - w->x;
                drag_off_y = my - w->y;
                ui_dirty = 1;
                return;
            }

            /* Uygulama Ici Tiklamalar */
            if (w->id == UI_WIN_CALC) {
                int start_x = w->x + 14;
                int start_y = w->y + 76;
                int btn_w = 48;
                int btn_h = 34;
                int gap_x = 6;
                int gap_y = 6;

                static const char *calc_chars[5][4] = {
                    { "C", "(", ")", "/" },
                    { "7", "8", "9", "*" },
                    { "4", "5", "6", "-" },
                    { "1", "2", "3", "+" },
                    { "0", "0", ".", "=" }
                };

                for (int r = 0; r < 5; r++) {
                    for (int c = 0; c < 4; c++) {
                        if (r == 4 && c == 1) continue;
                        int bx = start_x + c * (btn_w + gap_x);
                        int by = start_y + r * (btn_h + gap_y);
                        int bw = (r == 4 && c == 0) ? (btn_w * 2 + gap_x) : btn_w;

                        if (mx >= bx && mx <= bx + bw && my >= by && my <= by + btn_h) {
                            calc_input_char(calc_chars[r][c][0]);
                            ui_dirty = 1;
                            return;
                        }
                    }
                }
            } else if (w->id == UI_WIN_EXPLORER) {
                int ex = w->x + 10;
                int ey = w->y + 30;
                int ew = w->w - 20;

                /* Geri Butonu */
                if (mx >= ex && mx <= ex + 60 && my >= ey && my <= ey + 24) {
                    if (explorer_dir && explorer_dir->parent) {
                        explorer_dir = explorer_dir->parent;
                        ui_dirty = 1;
                    }
                    return;
                }

                /* Yeni Not Butonu */
                if (mx >= ex + ew - 82 && mx <= ex + ew && my >= ey && my <= ey + 24) {
                    vfs_create_file(explorer_dir, "yeni_not.txt", "Buraya yazin...\n");
                    ui_notepad_open_file("yeni_not.txt");
                    ui_window_open(UI_WIN_NOTEPAD);
                    ui_dirty = 1;
                    return;
                }

                /* Dosya/Klasor Listesi */
                int list_y = ey + 32;
                int row_y = list_y + 8;
                vfs_node_t *cur = explorer_dir ? explorer_dir->first_child : 0;

                while (cur && row_y + 20 < list_y + (w->h - 72)) {
                    if (my >= row_y && my <= row_y + 20) {
                        if (cur->type == VFS_TYPE_DIR) {
                            explorer_dir = cur;
                            ui_dirty = 1;
                            return;
                        } else {
                            /* Not defterinde ac */
                            char fpath[64];
                            vfs_get_path(cur, fpath, sizeof(fpath));
                            ui_notepad_open_file(fpath);
                            ui_window_open(UI_WIN_NOTEPAD);
                            ui_dirty = 1;
                            return;
                        }
                    }
                    row_y += 24;
                    cur = cur->next_sibling;
                }
            } else if (w->id == UI_WIN_NOTEPAD) {
                int nx = w->x + 10;
                int ny = w->y + 30;

                /* Kaydet */
                if (mx >= nx + 168 && mx <= nx + 238 && my >= ny && my <= ny + 24) {
                    notepad_save();
                    ui_dirty = 1;
                    return;
                }
                /* Temizle */
                if (mx >= nx + 244 && mx <= nx + 318 && my >= ny && my <= ny + 24) {
                    notepad_buf[0] = '\0';
                    notepad_len = 0;
                    notepad_cursor = 0;
                    ui_dirty = 1;
                    return;
                }
            } else if (w->id == UI_WIN_PAINT) {
                int px = w->x + 10;
                int py = w->y + 28;
                int pw = w->w - 20;

                /* Renk Paleti */
                static const uint8_t colors[8] = {
                    GFX_WHITE, GFX_BLACK, GFX_RED, GFX_GREEN,
                    GFX_BLUE, GFX_YELLOW, GFX_CYAN, GFX_MAGENTA
                };
                for (int c = 0; c < 8; c++) {
                    int cx = px + c * 22;
                    if (mx >= cx && mx <= cx + 18 && my >= py && my <= py + 18) {
                        paint_color = colors[c];
                        paint_is_eraser = 0;
                        ui_dirty = 1;
                        return;
                    }
                }

                /* Kalem */
                int tool_x = px + 184;
                if (mx >= tool_x && mx <= tool_x + 46 && my >= py && my <= py + 20) {
                    paint_is_eraser = 0;
                    ui_dirty = 1;
                    return;
                }
                /* Silgi */
                if (mx >= tool_x + 50 && mx <= tool_x + 96 && my >= py && my <= py + 20) {
                    paint_is_eraser = 1;
                    ui_dirty = 1;
                    return;
                }
                /* Firca 1 */
                if (mx >= tool_x + 102 && mx <= tool_x + 124 && my >= py && my <= py + 20) {
                    paint_brush = 1; ui_dirty = 1; return;
                }
                /* Firca 2 */
                if (mx >= tool_x + 128 && mx <= tool_x + 150 && my >= py && my <= py + 20) {
                    paint_brush = 2; ui_dirty = 1; return;
                }
                /* Firca 3 */
                if (mx >= tool_x + 154 && mx <= tool_x + 176 && my >= py && my <= py + 20) {
                    paint_brush = 3; ui_dirty = 1; return;
                }
                /* Temizle */
                if (mx >= px + pw - 48 && mx <= px + pw && my >= py && my <= py + 20) {
                    paint_init();
                    ui_dirty = 1;
                    return;
                }
                /* Tuvale Tiklama */
                int canvas_y = py + 26;
                if (mx >= px && mx < px + pw && my >= canvas_y && my < canvas_y + (w->h - 60)) {
                    paint_plot(mx - px, my - canvas_y);
                    ui_dirty = 1;
                    return;
                }
            } else if (w->id == UI_WIN_SETTINGS) {
                int sx = w->x + 12;
                int sy = w->y + 32;
                int sw = w->w - 24;
                int card_w = (sw - 3 * 8) / 4;
                int card_h = (w->w >= 600) ? 84 : 48;
                int card_y = sy + 30;
                int preview_y = card_y + 14;

                for (int i = 0; i < 4; i++) {
                    int cx = sx + i * (card_w + 8);
                    if (mx >= cx && mx <= cx + card_w &&
                        my >= preview_y && my <= preview_y + card_h + 18) {
                        wallpaper_mode = i;
                        ui_dirty = 1;
                        return;
                    }
                }
            } else if (w->id == UI_WIN_SEARCH) {
                int field_x = w->x + SEARCH_PAD;
                int field_w = w->w - 2 * SEARCH_PAD - SEARCH_BTN_W - 8;
                int field_y = w->y + SEARCH_FIELD_TOP;
                int btn_x = field_x + field_w + 8;

                /* Arama alani: pencere zaten aktif, imlec gorunur. */
                if (mx >= field_x && mx <= field_x + field_w &&
                    my >= field_y && my <= field_y + SEARCH_FIELD_H) {
                    ui_dirty = 1;
                    return;
                }

                /* Ara butonu */
                if (mx >= btn_x && mx <= btn_x + SEARCH_BTN_W &&
                    my >= field_y && my <= field_y + SEARCH_FIELD_H) {
                    search_run();
                    ui_dirty = 1;
                    return;
                }

                /* Sonuc satirlari */
                int row_x = w->x + 20;
                int row_w = w->w - 40;

                /* Detay karti aciksa yalnizca Geri butonu ve kart disi tiklama. */
                if (search_detail >= 0 && search_detail < search_result_count) {
                    int dy = w->y + SEARCH_RESULTS_TOP;
                    int back_x = row_x + row_w - 68;
                    int back_y = dy + 8;
                    if (mx >= back_x && mx <= back_x + 52 &&
                        my >= back_y && my <= back_y + 22) {
                        search_detail = -1;
                    }
                    ui_dirty = 1;
                    return;
                }

                for (int r = 0; r < search_result_count; r++) {
                    int row_y = w->y + SEARCH_RESULTS_TOP + r * SEARCH_ROW_H;
                    if (mx >= row_x && mx <= row_x + row_w &&
                        my >= row_y && my <= row_y + SEARCH_ROW_H - 4) {
                        search_activate(r);
                        return;
                    }
                }
            } else if (w->id == UI_WIN_MINES) {
                int min_x = w->x + 16;
                int min_y = w->y + 32;

                /* Gulucek / Reset Butonu */
                int face_x = min_x + 92;
                int face_y = min_y + 6;
                if (mx >= face_x && mx <= face_x + 24 && my >= face_y && my <= face_y + 20) {
                    mines_init_game();
                    ui_dirty = 1;
                    return;
                }

                /* Hucre Tiklamasi */
                int grid_y = min_y + 40;
                int cell_size = 22;

                for (int r = 0; r < MINES_ROWS; r++) {
                    for (int c = 0; c < MINES_COLS; c++) {
                        int cx = min_x + c * cell_size + 4;
                        int cy = grid_y + r * cell_size;

                        if (mx >= cx && mx <= cx + cell_size && my >= cy && my <= cy + cell_size) {
                            /* Sag tiklama veya Shift basiliysa bayrak, degilse ac */
                            int bstate;
                            mouse_get_state(0, 0, &bstate);
                            if (bstate & 0x02) {
                                mines_toggle_flag(r, c);
                            } else {
                                mines_reveal(r, c);
                            }
                            ui_dirty = 1;
                            return;
                        }
                    }
                }
            }

            return;
        }
    }

    /* 5. Masaustu Simgelerine Tiklandi mi? */
    if (mx >= 18 && mx <= 66) {
        if (my >= 12 && my <= 56) { ui_window_open(UI_WIN_TERMINAL); return; }
        if (my >= 72 && my <= 116) { ui_window_open(UI_WIN_EXPLORER); return; }
        if (my >= 132 && my <= 176) { ui_window_open(UI_WIN_NOTEPAD); return; }
        if (my >= 192 && my <= 236) { ui_window_open(UI_WIN_PAINT); return; }
        if (my >= 252 && my <= 296) { ui_window_open(UI_WIN_MINES); return; }
        if (my >= 312 && my <= 356) { ui_window_open(UI_WIN_CALC); return; }
        if (my >= 372 && my <= 416) { ui_window_open(UI_WIN_SYSINFO); return; }
        if (my >= 432 && my <= 476) { ui_window_open(UI_WIN_SEARCH); return; }
        if (my >= 492 && my <= 536) { ui_window_open(UI_WIN_SETTINGS); return; }
    }
}

/* ========================================================================= */
/* Baslatma ve Dongu                                                         */
/* ========================================================================= */
void ui_init(void)
{
    const boot_info_t *bi = boot_info_get();

    uint32_t fb = 0x000A0000;
    uint32_t font = 0x00009300;
    uint16_t width = 800;
    uint16_t height = 600;
    uint16_t pitch = 1600;
    uint8_t bpp = 16;

    if (bi->magic == BOOTINFO_MAGIC) {
        if (bi->fb_addr)   fb = bi->fb_addr;
        if (bi->font_addr) font = bi->font_addr;
        if (bi->width)     width = bi->width;
        if (bi->height)    height = bi->height;
        if (bi->pitch)     pitch = bi->pitch;
        if (bi->bpp)       bpp = bi->bpp;
    }

    gfx_init(fb, width, height, pitch, bpp, font);

    ui_term_clear();
    rtc_format_time(clock_text);
    paint_init();
    mines_init_game();
    search_run();

    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);
    string_format(mouse_text, sizeof(mouse_text), "F:%d,%d", mx, my);

    ui_dirty = 1;
    ui_render();
}

void ui_poll(void)
{
    if (!gfx_ready()) {
        return;
    }

    uint32_t sec = timer_seconds();
    if (sec != last_second) {
        last_second = sec;
        rtc_format_time(clock_text);
        ui_dirty = 1;
    }

    search_poll_net();

    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);

    int just_pressed = ((buttons & 1) && !(last_buttons & 1));
    int just_released = (!(buttons & 1) && (last_buttons & 1));
    int is_held = (buttons & 1);

    if (mx != last_mouse_x || my != last_mouse_y || buttons != last_buttons) {
        last_mouse_x = mx;
        last_mouse_y = my;
        last_buttons = buttons;
        string_format(mouse_text, sizeof(mouse_text), "F:%d,%d", mx, my);
        ui_dirty = 1;
    }

    handle_mouse_input(mx, my, just_pressed, just_released, is_held);

    if (ui_dirty) {
        ui_dirty = 0;
        ui_render();
    }
}
