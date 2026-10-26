#include "pmm.h"
#include "bootinfo.h"
#include "console.h"
#include "string.h"
#include "types.h"

#define MAX_FRAMES  (1024u * 1024u)          /* 4 GB / 4 KB */
#define BITMAP_SIZE (MAX_FRAMES / 8)

extern uint8_t kernel_end;

static uint8_t  bitmap[BITMAP_SIZE];
static uint32_t total_frames;
static uint32_t free_frames;
static uint32_t highest_frame;

static inline void bitmap_set(uint32_t frame)
{
    bitmap[frame >> 3] |= (uint8_t)(1u << (frame & 7));
}

static inline void bitmap_clear(uint32_t frame)
{
    bitmap[frame >> 3] &= (uint8_t)~(1u << (frame & 7));
}

static inline int bitmap_test(uint32_t frame)
{
    return (bitmap[frame >> 3] >> (frame & 7)) & 1;
}

static void mark_usable(uint64_t base, uint64_t length)
{
    uint64_t end = base + length;

    if (base >= 0x100000000ull) {
        return;
    }
    if (end > 0x100000000ull) {
        end = 0x100000000ull;
    }

    uint64_t start_frame = (base + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t end_frame   = end / PAGE_SIZE;

    if (end_frame > MAX_FRAMES) {
        end_frame = MAX_FRAMES;
    }

    for (uint64_t f = start_frame; f < end_frame; f++) {
        if (bitmap_test((uint32_t)f)) {
            bitmap_clear((uint32_t)f);
            free_frames++;
            total_frames++;
            if ((uint32_t)f > highest_frame) {
                highest_frame = (uint32_t)f;
            }
        }
    }
}

static void reserve_range(uint32_t start, uint32_t end)
{
    uint32_t first = start / PAGE_SIZE;
    uint32_t last  = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    if (last > MAX_FRAMES) {
        last = MAX_FRAMES;
    }

    for (uint32_t f = first; f < last; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            if (free_frames > 0) {
                free_frames--;
            }
        }
    }
}

void pmm_init(void)
{
    memset(bitmap, 0xFF, sizeof(bitmap));   /* once hepsi dolu */
    total_frames  = 0;
    free_frames   = 0;
    highest_frame = 0;

    const boot_info_t *bi = boot_info_get();

    if (bi->magic == BOOTINFO_MAGIC && bi->e820_count > 0) {
        const e820_entry_t *entries = (const e820_entry_t *)bi->e820_addr;

        for (uint32_t i = 0; i < bi->e820_count && i < E820_MAX_ENTRIES; i++) {
            if (entries[i].type == 1) {
                mark_usable(entries[i].base, entries[i].length);
            }
        }
    } else {
        kprintf("UYARI: E820 haritasi yok, 32 MB varsayiliyor.\n");
        mark_usable(0x100000, 32u * 1024u * 1024u - 0x100000);
    }

    /* Dusuk bellek: BIOS, VGA, boot sector, stage2, kernel yukleme tamponu */
    reserve_range(0, 0x100000);

    /* Cekirdek imaji + .bss (heap, bitmap, sayfa tablolari, yigin) */
    uint32_t kernel_start = 0x100000;
    uint32_t kernel_finish = ((uint32_t)&kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    reserve_range(kernel_start, kernel_finish);

    kprintf("PMM: %u kare, %u bos, %u kullanilan (%u MB)\n",
            total_frames, free_frames, total_frames - free_frames,
            (uint32_t)((uint64_t)total_frames * PAGE_SIZE / (1024 * 1024)));
}

uint32_t pmm_alloc_frame(void)
{
    for (uint32_t f = 0; f < MAX_FRAMES; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            if (free_frames > 0) {
                free_frames--;
            }
            return f * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint32_t address)
{
    if (address == 0 || (address & (PAGE_SIZE - 1)) != 0) {
        return;
    }

    uint32_t frame = address / PAGE_SIZE;
    if (frame >= MAX_FRAMES) {
        return;
    }

    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        free_frames++;
    }
}

uint32_t pmm_total_frames(void)
{
    return total_frames;
}

uint32_t pmm_free_frames(void)
{
    return free_frames;
}

uint32_t pmm_used_frames(void)
{
    return total_frames - free_frames;
}

uint64_t pmm_total_bytes(void)
{
    return (uint64_t)total_frames * PAGE_SIZE;
}

uint32_t pmm_highest_address(void)
{
    return (highest_frame + 1) * PAGE_SIZE;
}
