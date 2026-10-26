#include "heap.h"
#include "console.h"
#include "string.h"
#include "types.h"

#define HEAP_SIZE   (256u * 1024u)
#define HEAP_MAGIC  0xC0FFEE42u

typedef struct block {
    uint32_t      size;         /* kullanilabilir bayt */
    uint32_t      magic;
    struct block *next;
    uint32_t      free;
} block_t;

static uint8_t  heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static block_t *heap_head;
static uint32_t used_bytes;

static inline uint32_t align8(uint32_t value)
{
    return (value + 7u) & ~7u;
}

void heap_init(void)
{
    heap_head = (block_t *)heap_area;
    heap_head->size  = HEAP_SIZE - sizeof(block_t);
    heap_head->magic = HEAP_MAGIC;
    heap_head->next  = NULL;
    heap_head->free  = 1;
    used_bytes = 0;
}

void *kmalloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    uint32_t wanted = align8((uint32_t)size);
    block_t *block = heap_head;

    while (block) {
        if (block->free && block->size >= wanted) {
            /* Yeterli yer varsa blogu ikiye bol */
            if (block->size >= wanted + sizeof(block_t) + 8) {
                block_t *rest = (block_t *)((uint8_t *)block + sizeof(block_t) + wanted);
                rest->size  = block->size - wanted - sizeof(block_t);
                rest->magic = HEAP_MAGIC;
                rest->next  = block->next;
                rest->free  = 1;

                block->size = wanted;
                block->next = rest;
            }

            block->free = 0;
            used_bytes += block->size;
            return (uint8_t *)block + sizeof(block_t);
        }
        block = block->next;
    }

    return NULL;
}

static void coalesce(void)
{
    block_t *block = heap_head;

    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(block_t) + block->next->size;
            block->next = block->next->next;
        } else {
            block = block->next;
        }
    }
}

void kfree(void *ptr)
{
    if (!ptr) {
        return;
    }

    block_t *block = (block_t *)((uint8_t *)ptr - sizeof(block_t));

    if ((uint8_t *)block < heap_area ||
        (uint8_t *)block >= heap_area + HEAP_SIZE) {
        kprintf("heap: gecersiz isaretci\n");
        return;
    }

    if (block->magic != HEAP_MAGIC) {
        kprintf("heap: bozuk blok basligi\n");
        return;
    }

    if (block->free) {
        kprintf("heap: cift free\n");
        return;
    }

    block->free = 1;
    if (used_bytes >= block->size) {
        used_bytes -= block->size;
    } else {
        used_bytes = 0;
    }

    coalesce();
}

uint32_t heap_total_bytes(void)
{
    return HEAP_SIZE;
}

uint32_t heap_used_bytes(void)
{
    return used_bytes;
}

uint32_t heap_free_bytes(void)
{
    return HEAP_SIZE - used_bytes;
}

int heap_self_test(void)
{
    uint32_t before = used_bytes;

    void *a = kmalloc(64);
    void *b = kmalloc(1024);
    void *c = kmalloc(4096);

    if (!a || !b || !c) {
        return 0;
    }

    memset(a, 0xAA, 64);
    memset(b, 0xBB, 1024);
    memset(c, 0xCC, 4096);

    kfree(b);
    kfree(a);
    kfree(c);

    return used_bytes == before;
}
