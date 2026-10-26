#ifndef CASREV_HEAP_H
#define CASREV_HEAP_H

#include "types.h"

void     heap_init(void);
void    *kmalloc(size_t size);
void     kfree(void *ptr);

uint32_t heap_total_bytes(void);
uint32_t heap_used_bytes(void);
uint32_t heap_free_bytes(void);
int      heap_self_test(void);

#endif
