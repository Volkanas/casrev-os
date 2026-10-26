; =============================================================================
; casrev-os - Asama 2: Onyukleme Sektoru (Boot Sector)
; =============================================================================
; BIOS bu sektoru 0x7C00 adresine yukler ve 16-bit gercek modda calistirir.
;
; Yaptiklari:
;   - Segmentleri ve yigini kurar
;   - VGA metin modunu secer, COM1'i baslatir
;   - Karsilama mesajini yazar
;   - Diskten stage2'yi (sektor 2'den itibaren) 0x7E00'a yukler
;   - stage2'ye atlar
;
; Derleme: nasm -f bin -D STAGE2_SECTORS=n -o boot.bin boot.asm
; =============================================================================

BITS 16
ORG 0x7C00

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 4
%endif

STAGE2_OFFSET equ 0x7E00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl        ; BIOS onyukleme surucusunu DL'de verir

    mov ax, 0x0003
    int 0x10

    call serial_init

    mov si, msg_banner
    call print
    mov si, msg_line
    call print
    mov si, msg_ready
    call print
    mov si, msg_loading
    call print

    call load_stage2

    mov si, msg_ok
    call print

    mov dl, [boot_drive]        ; BIOS surucu numarasini stage2'ye devret
    jmp 0x0000:STAGE2_OFFSET    ; stage2'ye devret

; -----------------------------------------------------------------------------
; load_stage2: STAGE2_SECTORS kadar sektoru diskten 0x0000:0x7E00'a okur.
; CHS: sektor 2'den baslar, track sonunda sonraki head'e gecer.
; -----------------------------------------------------------------------------
load_stage2:
    xor ah, ah                  ; disk reset
    mov dl, [boot_drive]
    int 0x13

    mov ax, 0x0000
    mov es, ax
    mov bx, STAGE2_OFFSET
    mov cx, 2                   ; ilk sektor (1 tabanli)
    xor dh, dh                  ; head 0
    mov si, STAGE2_SECTORS

.next_sector:
    mov dl, [boot_drive]
    mov ah, 0x02                ; sektor oku
    mov al, 1
    int 0x13
    jc .error

    add bx, 512
    inc cx
    cmp cx, 19                  ; 1.44 MB disket: 18 sektor/track
    jb .advance
    mov cx, 1                   ; sonraki track
    xor dh, 1                   ; head degistir
.advance:
    dec si
    jnz .next_sector
    ret

.error:
    mov si, msg_disk_error
    call print
    cli
    hlt
    jmp $

; -----------------------------------------------------------------------------
; print / putc / serial_init: Asama 1 ile ayni.
; -----------------------------------------------------------------------------
print:
    push ax
.next:
    lodsb
    test al, al
    jz .done
    call putc
    jmp .next
.done:
    pop ax
    ret

putc:
    mov [char], al

    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10

    mov dx, 0x3FD
.wait_tx:
    in al, dx
    test al, 0x20
    jz .wait_tx

    mov dx, 0x3F8
    mov al, [char]
    out dx, al
    ret

serial_init:
    push ax
    push dx
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x01
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Veri
; -----------------------------------------------------------------------------
boot_drive: db 0
char:       db 0

msg_banner:     db "casrev-os v0.9", 13, 10, 0
msg_line:       db "-----------------------------", 13, 10, 0
msg_ready:      db "Onyukleme sektoru calisiyor.", 13, 10, 0
msg_loading:    db "stage2 diskten yukleniyor...", 13, 10, 0
msg_ok:         db "stage2 yuklendi, devrediliyor.", 13, 10, 0
msg_disk_error: db "HATA: disk okuma basarisiz!", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
