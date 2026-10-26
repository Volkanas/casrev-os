#ifndef CASREV_KEYBOARD_H
#define CASREV_KEYBOARD_H

/* Ozel tuslar: keyboard_getchar() tarafindan dondurulur. */
#define KEY_UP   0x11
#define KEY_DOWN 0x12
#define KEY_PGUP 0x13
#define KEY_PGDN 0x14
#define KEY_HOME 0x15
#define KEY_END  0x16

void keyboard_init(void);
void keyboard_handler(void);

/* Buffer'da karakter varsa dondurur, yoksa -1. */
int keyboard_getchar(void);

#endif
