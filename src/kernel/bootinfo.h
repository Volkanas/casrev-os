#ifndef CASREV_BOOTINFO_H
#define CASREV_BOOTINFO_H

#include "types.h"

#define BOOTINFO_ADDR   0x8F00
#define BOOTINFO_MAGIC  0x43415352   /* 'CASR' */
#define E820_MAX_ENTRIES 32

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;          /* 1 = kullanilabilir RAM */
    uint32_t acpi;          /* ACPI 3.0 genisletilmis ozellikler */
} __attribute__((packed)) e820_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t e820_count;
    uint32_t e820_addr;
    uint32_t font_addr;     /* BIOS 8x8 font (256 * 8 bayt) */
    uint32_t fb_addr;       /* dogrusal framebuffer */
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  reserved0;
    uint16_t pitch;
    uint32_t reserved1;
} __attribute__((packed)) boot_info_t;

static inline const boot_info_t *boot_info_get(void)
{
    return (const boot_info_t *)BOOTINFO_ADDR;
}

#endif
