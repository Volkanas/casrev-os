#ifndef CASREV_UI_H
#define CASREV_UI_H

#include "types.h"

/* Pencere Numaralari */
#define UI_WIN_TERMINAL 0
#define UI_WIN_SYSINFO  1
#define UI_WIN_CALC     2
#define UI_WIN_EXPLORER 3
#define UI_WIN_NOTEPAD  4
#define UI_WIN_PAINT    5
#define UI_WIN_MINES    6
#define UI_WIN_SETTINGS 7
#define UI_WIN_SEARCH   8
#define UI_WIN_BROWSER  9
#define UI_WIN_COUNT    10

void ui_init(void);
void ui_poll(void);
void ui_render(void);

void ui_term_clear(void);
void ui_term_putc(char c);
void ui_term_set_color(uint8_t fg, uint8_t bg);

/* Pencere ve Menü Arayüz Yönetimi */
void ui_window_open(int win_id);
void ui_window_close(int win_id);
void ui_window_toggle(int win_id);
void ui_start_menu_toggle(void);
int  ui_handle_key(int key);

/* Not Defteri icin dosya yukleme */
void ui_notepad_open_file(const char *path);

/* Yerel arama penceresini istege bagli sorguyla acar */
void ui_search_open(const char *query);

#endif
