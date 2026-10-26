#ifndef CASREV_PAGING_H
#define CASREV_PAGING_H

#include "types.h"

void     paging_init(uint32_t map_bytes);
int      paging_enabled(void);
uint32_t paging_mapped_bytes(void);
int      paging_self_test(void);

#endif
