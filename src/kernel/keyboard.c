#include "keyboard.h"
#include "io.h"
#include "types.h"

#define BUFFER_SIZE 256

/*
 * Turkce Q (KBDTUQ) scancode set 1.
 *
 * Onemli ozel karakterler (ISO-8859-9 / CP1254 kodlari):
 *   ccedilla 0xE7 / 0xC7, gbreve 0xF0 / 0xD0, dotlessi 0xFD,
 *   Idotabove 0xDD, odiaeresis 0xF6 / 0xD6, scedilla 0xFE / 0xDE,
 *   udiaeresis 0xFC / 0xDC.
 */
static volatile uint8_t buffer[BUFFER_SIZE];
static volatile uint8_t head;
static volatile uint8_t tail;

static int shift;
static int caps;
static int altgr;
static int extended;

static const unsigned char keymap[128] = {
    [0x01] = 27, /* ESC */
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '*', [0x0D] = '-',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 0xFD, /* dotless i */
    [0x18] = 'o', [0x19] = 'p',
    [0x1A] = 0xF0, /* g breve */
    [0x1B] = 0xFC, /* u diaeresis */
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = 0xFE, /* s cedilla */
    [0x28] = 'i',
    [0x29] = '\\',
    [0x2B] = ',',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = 0xF6, /* o diaeresis */
    [0x34] = 0xE7, /* c cedilla */
    [0x35] = '.',
    [0x39] = ' '
};

static const unsigned char shiftmap[128] = {
    [0x02] = '!', [0x03] = '\'', [0x04] = '^', [0x05] = '$', [0x06] = '%',
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=',
    [0x0C] = '?', [0x0D] = '_',
    [0x29] = '"', [0x2B] = ';', [0x35] = ':',
    [0x39] = ' '
};

/* AltGr (sag Alt) sembolleri. */
static const unsigned char altgr_map[128] = {
    [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x08] = '{', [0x09] = '[', [0x0A] = ']', [0x0B] = '}',
    [0x0C] = '\\', [0x29] = '~', [0x2B] = '|',
    [0x33] = '<', [0x34] = '>', [0x56] = '|'
};

static int is_lower_letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') ||
           c == 0xE7 || c == 0xF0 || c == 0xF6 ||
           c == 0xFC || c == 0xFD || c == 0xFE;
}

static unsigned char to_upper_tr(unsigned char c)
{
    /* Turkce'de kucuk 'i' harfinin buyugu ASCII 'I' degil, noktali 'I'dir. */
    if (c == 'i') {
        return 0xDD; /* I dot above */
    }
    if (c >= 'a' && c <= 'z') {
        return (unsigned char)(c - 'a' + 'A');
    }

    switch (c) {
    case 0xE7: return 0xC7; /* c cedilla */
    case 0xF0: return 0xD0; /* g breve */
    case 0xF6: return 0xD6; /* o diaeresis */
    case 0xFC: return 0xDC; /* u diaeresis */
    case 0xFD: return 'I';  /* dotless i -> I */
    case 0xFE: return 0xDE; /* s cedilla */
    default:   return c;
    }
}

static void buffer_push(unsigned char c)
{
    uint8_t next = (uint8_t)((head + 1) & (BUFFER_SIZE - 1));

    if (next != tail) {
        buffer[head] = c;
        head = next;
    }
}

void keyboard_init(void)
{
    head = 0;
    tail = 0;
    shift = 0;
    caps = 0;
    altgr = 0;
    extended = 0;

    /* IRQ1'i maskelemeyi kaldir (PIC zaten acmis olsa da garanti olsun) */
    uint8_t mask = inb(0x21);
    outb(0x21, (uint8_t)(mask & ~0x02));

    /* Bekleyen scancode varsa temizle */
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }
}

void keyboard_handler(void)
{
    /* 8042 status biti 5: veri AUX (fare) portundan geldi. */
    if (inb(0x64) & 0x20u) {
        return;
    }

    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    if (extended) {
        extended = 0;

        /* Sag Alt (AltGr) basildi/birakildi. */
        if (scancode == 0x38) {
            altgr = 1;
        } else if (scancode == 0xB8) {
            altgr = 0;
        }
        return;
    }

    /* Tus birakma */
    if (scancode & 0x80) {
        uint8_t released = (uint8_t)(scancode & 0x7F);
        if (released == 0x2A || released == 0x36) {
            shift = 0;
        }
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift = 1;
        return;
    }

    if (scancode == 0x3A) {
        caps = !caps;
        return;
    }

    unsigned char base = keymap[scancode];
    if (base == 0) {
        return;
    }

    unsigned char c;

    if (is_lower_letter(base)) {
        /* Turkce Q'da i -> I, i -> Idotabove seklinde ozel esleme gerekir. */
        if (base == 'i') {
            c = (shift ^ caps) ? 0xDD : 'i';
        } else if (base == 0xFD) {
            c = (shift ^ caps) ? 'I' : 0xFD;
        } else {
            c = (shift ^ caps) ? to_upper_tr(base) : base;
        }
    } else if (altgr && altgr_map[scancode]) {
        c = altgr_map[scancode];
    } else {
        c = shift ? shiftmap[scancode] : base;
        if (c == 0) {
            c = base;
        }
    }

    if (c) {
        buffer_push(c);
    }
}

int keyboard_getchar(void)
{
    if (tail == head) {
        return -1;
    }

    char c = (char)buffer[tail];
    tail = (uint8_t)((tail + 1) & (BUFFER_SIZE - 1));
    return (int)(uint8_t)c;
}
