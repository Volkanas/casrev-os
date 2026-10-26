#ifndef CASREV_KEYBOARD_H
#define CASREV_KEYBOARD_H

void keyboard_init(void);
void keyboard_handler(void);

/* Buffer'da karakter varsa dondurur, yoksa -1. */
int keyboard_getchar(void);

#endif
