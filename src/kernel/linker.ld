/* =============================================================================
 * casrev-os - baglayici betigi
 * Cekirdek 1 MB (0x100000) adresine yuklenir.
 * ========================================================================== */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
