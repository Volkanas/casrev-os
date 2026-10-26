#ifndef CASREV_PMM_H
#define CASREV_PMM_H

#include "types.h"

#define PAGE_SIZE 4096

void pmm_init(void);

uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t address);

uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_used_frames(void);
uint64_t pmm_total_bytes(void);
uint32_t pmm_highest_address(void);

#endif
