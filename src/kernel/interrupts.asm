; =============================================================================
; casrev-os - Kesme (interrupt) giris noktalari
; =============================================================================
; 0-31 : CPU istisnalari
; 32-47: PIC uzerinden IRQ'lar (0x20-0x2F'ye tasindi)
;
; Ortak yol:
;   - Kesme kapisi IF'i zaten temizler
;   - Hata kodu olmayan vektorler icin sahte 0 push edilir
;   - Vektor numarasi ve registerlar yigina alinir
;   - interrupt_dispatch(registers_t*) cagrilir
;   - iret ile kesilen yere donulur
; =============================================================================

BITS 32

section .text
extern interrupt_dispatch

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0                ; sahte hata kodu
    push dword %1               ; vektor
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1               ; CPU hata kodunu zaten push etti
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; IRQ 0-15 -> vektor 32-47
%assign vec 32
%rep 16
    ISR_NOERR vec
%assign vec vec+1
%endrep

isr_common:
    pusha                       ; eax, ecx, edx, ebx, esp, ebp, esi, edi
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10                ; flat veri segmenti
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; registers_t *arg
    call interrupt_dispatch
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                  ; vektor + hata kodu
    iret

; -----------------------------------------------------------------------------
; C tarafinda IDT kurarken kullanilan adres tablosu (48 giris).
; -----------------------------------------------------------------------------
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dd isr%+i
%assign i i+1
%endrep
