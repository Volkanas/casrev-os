#ifndef CASREV_MOUSE_H
#define CASREV_MOUSE_H

void mouse_init(void);
void mouse_handler(void);
void mouse_get_state(int *x, int *y, int *buttons);

#endif
