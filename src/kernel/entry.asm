; =============================================================================
; casrev-os - Cekirdek giris noktasi (32-bit)
; =============================================================================
; stage2 tarafindan 0x100000 adresine kopyalandiktan sonra buradan baslar.
; Yigini kurar, .bss bolumunu sifirlar ve kernel_main()'i cagirir.
; =============================================================================

BITS 32

section .text
global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
    cli

    mov esp, stack_top

    ; .bss bolumunu sifirla (flat binary'de yer kaplamaz)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
