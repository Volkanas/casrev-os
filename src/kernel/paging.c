#include "paging.h"
#include "bootinfo.h"
#include "console.h"
#include "string.h"
#include "types.h"

#define MAX_MAP_MB 64
#define MAX_TABLES (MAX_MAP_MB / 4)
#define MAX_FB_TABLES 4

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[MAX_TABLES][1024] __attribute__((aligned(4096)));
static uint32_t fb_page_tables[MAX_FB_TABLES][1024] __attribute__((aligned(4096)));

static uint32_t mapped_bytes;
static int      enabled;

void paging_init(uint32_t map_bytes)
{
    if (map_bytes > MAX_MAP_MB * 1024u * 1024u) {
        map_bytes = MAX_MAP_MB * 1024u * 1024u;
    }

    /* 4 MB'a yukari yuvarla (her sayfa tablosu 4 MB kapsar) */
    map_bytes = (map_bytes + 0x3FFFFF) & ~0x3FFFFFu;
    if (map_bytes == 0) {
        map_bytes = 0x400000;
    }

    memset(page_directory, 0, sizeof(page_directory));

    uint32_t tables = map_bytes >> 22;

    for (uint32_t t = 0; t < tables; t++) {
        uint32_t *pt = page_tables[t];

        for (uint32_t i = 0; i < 1024; i++) {
            uint32_t phys = (t << 22) + (i << 12);
            pt[i] = phys | 0x003;       /* present + read/write */
        }

        page_directory[t] = ((uint32_t)pt) | 0x003;
    }

    /* Framebuffer (VESA Linear Frame Buffer) sayfalama eslemesi */
    const boot_info_t *bi = boot_info_get();
    if (bi && bi->magic == BOOTINFO_MAGIC && bi->fb_addr != 0) {
        uint32_t fb_base = bi->fb_addr & ~0x3FFFFFu;
        uint32_t fb_size = (uint32_t)bi->pitch * bi->height;
        if (fb_size < 0x400000) {
            fb_size = 0x400000;
        }
        uint32_t fb_tables = (fb_size + 0x3FFFFF) >> 22;
        if (fb_tables > MAX_FB_TABLES) {
            fb_tables = MAX_FB_TABLES;
        }

        for (uint32_t ft = 0; ft < fb_tables; ft++) {
            uint32_t base_phys = fb_base + (ft << 22);
            uint32_t pde_idx = base_phys >> 22;
            if (pde_idx < 1024 && page_directory[pde_idx] == 0) {
                uint32_t *pt = fb_page_tables[ft];
                for (uint32_t i = 0; i < 1024; i++) {
                    pt[i] = (base_phys + (i << 12)) | 0x003;
                }
                page_directory[pde_idx] = ((uint32_t)pt) | 0x003;
            }
        }
    }

    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint32_t)page_directory) : "memory");

    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                 /* CR0.PG */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    mapped_bytes = map_bytes;
    enabled = 1;
}

int paging_enabled(void)
{
    return enabled;
}

uint32_t paging_mapped_bytes(void)
{
    return mapped_bytes;
}

int paging_self_test(void)
{
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));

    if ((cr0 & 0x80000000u) == 0) {
        return 0;
    }

    /* Ilk sayfa tablosu ve ilk 4 KB eslenmis olmali */
    return (page_directory[0] & 1) && (page_tables[0][0] & 1);
}
