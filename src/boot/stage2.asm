; =============================================================================
; casrev-os - Asama 3: Ikinci Asama Yukleyici (stage2)
; =============================================================================
; Boot sector tarafindan 0x7E00 adresine yuklenir (4 sektor).
;
; Yaptiklari:
;   - Gercek modda mesaj yazar
;   - Diskten C cekirdegini (kernel.bin) 0x10000 adresine yukler
;   - A20 hattini acar (port 0x92, "fast A20")
;   - GDT'yi yukler, CR0.PE ile protected mode'a gecer
;   - Cekirdegi 1 MB adresine (0x100000) kopyalar ve atlar
; =============================================================================

BITS 16
ORG 0x7E00

CODE_SEL            equ 0x08
DATA_SEL            equ 0x10
KERNEL_LOAD_SEG     equ 0x1000      ; gecici yukleme: 0x10000
KERNEL_START_SECTOR equ 6           ; 1 tabanli CHS sektoru (boot+stage2 = 5)
KERNEL_ENTRY        equ 0x100000

BOOTINFO_ADDR       equ 0x8F00      ; boot_info_t (32 bayt)
E820_ADDR           equ 0x8F40      ; E820 girisleri (32 * 24 bayt)
FONT_ADDR           equ 0x9300      ; BIOS 8x8 font (256 * 8 bayt)
E820_MAX_ENTRIES    equ 32
BOOTINFO_MAGIC      equ 0x43415352  ; 'CASR'

; VESA BIOS Extensions (VBE) definitions
VBE_CTRL_INFO_ADDR  equ 0x9B00  ; Temporary buffer for VBE Controller Info (after FONT_ADDR 0x9300-0x9B00)
VBE_MODE_INFO_ADDR  equ 0x9D00  ; Temporary buffer for VBE Mode Info

; VBE Mode Info Structure offsets (used for reading values)
VBE_MI_MODE_ATTRIBUTES  equ 0x00
VBE_MI_X_RESOLUTION     equ 0x12
VBE_MI_Y_RESOLUTION     equ 0x14
VBE_MI_BYTES_PER_SCANLINE equ 0x10 ; This is pitch
VBE_MI_BITS_PER_PIXEL   equ 0x19
VBE_MI_LINEAR_FB_ADDR   equ 0x28

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 16
%endif

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    call serial_init

    mov si, msg_loaded
    call print_rm

    call detect_memory
    mov si, msg_memory
    call print_rm

    call copy_font
    call init_bootinfo

    call load_kernel
    mov si, msg_kernel
    call print_rm

    call a20_enable
    mov si, msg_a20
    call print_rm

    mov si, msg_pmode
    call print_rm

    call set_video_mode

    ; BIOS cagrisi segment registerlarini bozmus olabilir.
    xor ax, ax
    mov ds, ax
    mov es, ax

    cli
    lgdt [gdtr]

    mov eax, cr0
    or eax, 1                   ; CR0.PE = 1
    mov cr0, eax

    jmp dword CODE_SEL:pm32

; -----------------------------------------------------------------------------
; detect_memory: BIOS int 15h, EAX=E820 ile fiziksel bellek haritasini alir.
; Sonuc: BOOTINFO_ADDR'da magic + giris sayisi, E820_ADDR'da 24 baytlik girisler.
; -----------------------------------------------------------------------------
detect_memory:
    xor ax, ax
    mov es, ax
    mov di, E820_ADDR
    xor ebx, ebx
    mov edx, 0x534D4150         ; 'SMAP'
    mov word [e820_count], 0

.loop:
    mov eax, 0xE820
    mov ecx, 24
    mov dword [es:di+20], 1     ; ACPI 3.0 alanini gecerli isaretle
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp dword [es:di+16], 0     ; tip 0: gecersiz, dur
    je .done

    add di, 24
    inc word [e820_count]
    cmp word [e820_count], E820_MAX_ENTRIES
    jae .done

    test ebx, ebx
    jnz .loop

.done:
    mov dword [BOOTINFO_ADDR], BOOTINFO_MAGIC
    movzx eax, word [e820_count]
    mov [BOOTINFO_ADDR+4], eax
    mov dword [BOOTINFO_ADDR+8], E820_ADDR
    mov dword [BOOTINFO_ADDR+12], 0
    ret

; -----------------------------------------------------------------------------
; copy_font: BIOS'tan 8x8 VGA fontunu FONT_ADDR'a kopyalar.
;   int 10h AX=1130 BH=0 -> ES:BP font, CX karakter basina bayt (8).
;   Basarisizsa ROM'daki F000:FA6E adresine duser.
; -----------------------------------------------------------------------------
copy_font:
    push ds
    push es
    pushad

    mov ax, 0x1130
    mov bh, 0x00
    int 0x10

    cmp cx, 8
    jne .fallback
    mov ax, es
    test ax, ax
    jz .fallback

    mov ds, ax
    mov si, bp
    jmp .copy

.fallback:
    mov ax, 0xF000
    mov ds, ax
    mov si, 0xFA6E

.copy:
    xor ax, ax
    mov es, ax
    mov di, FONT_ADDR
    mov cx, 2048
    cld
    rep movsb

    popad
    pop es
    pop ds
    ret

; -----------------------------------------------------------------------------
; init_bootinfo: ekran/font bilgilerini boot_info_t'ye yazar.
; -----------------------------------------------------------------------------
init_bootinfo:
    ; Bu fonksiyonun icerigi VESA mod bilgileri ile set_video_mode tarafindan guncellenecektir.
    ; Baslangic degerlerini 0 olarak ayarla veya bilinen bir text modu icin tut.
    ; Simdilik font adresini ayarliyoruz.
    mov dword [BOOTINFO_ADDR+12], FONT_ADDR
    ; Diger alanlar set_video_mode tarafindan doldurulacaktir.
    xor eax, eax
    mov dword [BOOTINFO_ADDR+16], eax ; Framebuffer adresi
    mov word  [BOOTINFO_ADDR+20], ax  ; X Cozunurluk
    mov word  [BOOTINFO_ADDR+22], ax  ; Y Cozunurluk
    mov byte  [BOOTINFO_ADDR+24], al  ; Bits Per Pixel
    mov byte  [BOOTINFO_ADDR+25], al  ; Reserved
    mov word  [BOOTINFO_ADDR+26], ax  ; Bytes Per Scanline (Pitch)
    mov dword [BOOTINFO_ADDR+28], eax ; Reserved
    ret

; -----------------------------------------------------------------------------
; set_video_mode: VESA 800x600 (16-bit RGB565, Mode 0x114).
; -----------------------------------------------------------------------------
set_video_mode:
    ; Segment register'larini sifirla (genel register AX uzerinden)
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; VBE Mode 0x114 (800x600, 16-bit) bilgilerini al
    mov cx, 0x0114
    mov di, VBE_MODE_INFO_ADDR
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .error

    ; 16-bit renk derinligini dogrula
    cmp byte [VBE_MODE_INFO_ADDR + VBE_MI_BITS_PER_PIXEL], 16
    jne .error

    ; Linear Framebuffer (LFB) bitini (Bit 14, 0x4000) aktif ederek VESA Mode 0x114'e gec: 0x4114
    mov bx, 0x4114      ; 0x0114 | 0x4000 (LFB aktif)
    mov ax, 0x4F02      ; Set VBE mode
    int 0x10
    cmp ax, 0x004F
    jne .error

    ; Segmentleri sifirla (AX uzerinden)
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; boot_info_t yapisini VESA bilgileriyle guncelle
    ; 1. Framebuffer adresi (physbase - offset 0x28)
    mov eax, [VBE_MODE_INFO_ADDR + VBE_MI_LINEAR_FB_ADDR]
    mov [BOOTINFO_ADDR+16], eax

    ; 2. Genislik (X_RESOLUTION - offset 0x12)
    mov ax, [VBE_MODE_INFO_ADDR + VBE_MI_X_RESOLUTION]
    test ax, ax
    jnz .w_ok
    mov ax, 800
.w_ok:
    mov [BOOTINFO_ADDR+20], ax

    ; 3. Yukseklik (Y_RESOLUTION - offset 0x14)
    mov ax, [VBE_MODE_INFO_ADDR + VBE_MI_Y_RESOLUTION]
    test ax, ax
    jnz .h_ok
    mov ax, 600
.h_ok:
    mov [BOOTINFO_ADDR+22], ax

    ; 4. Bits Per Pixel (BITS_PER_PIXEL - offset 0x19)
    mov al, [VBE_MODE_INFO_ADDR + VBE_MI_BITS_PER_PIXEL]
    test al, al
    jnz .bpp_ok
    mov al, 16
.bpp_ok:
    mov [BOOTINFO_ADDR+24], al

    ; 5. Pitch (BYTES_PER_SCANLINE - offset 0x10)
    mov ax, [VBE_MODE_INFO_ADDR + VBE_MI_BYTES_PER_SCANLINE]
    test ax, ax
    jnz .p_ok
    mov ax, [BOOTINFO_ADDR+20]  ; width
    shl ax, 1                   ; 16-bit: 2 bytes per pixel
.p_ok:
    mov [BOOTINFO_ADDR+26], ax

    ret

.error:
    ; Hata durumunda, metin moduna (80x25) gec ve hata mesaji goster
    mov ax, 0x0003
    int 0x10
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, msg_vesa_error
    call print_rm
    cli
    hlt
    jmp $



; -----------------------------------------------------------------------------
; load_kernel: KERNEL_SECTORS kadar sektoru KERNEL_START_SECTOR'dan baslayarak
; KERNEL_LOAD_SEG:0000 adresine okur. Her sektorde ES 0x20 paragraph artar.
; -----------------------------------------------------------------------------
load_kernel:
    xor ah, ah                  ; disk reset
    mov dl, [boot_drive]
    int 0x13

    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx

    mov byte [sector], KERNEL_START_SECTOR
    mov byte [head], 0
    mov byte [cyl], 0
    mov word [remaining], KERNEL_SECTORS

.next_sector:
    mov dl, [boot_drive]
    mov ah, 0x02                ; sektor oku
    mov al, 1
    mov ch, [cyl]
    mov cl, [sector]
    mov dh, [head]
    int 0x13
    jc .error

    mov ax, es                  ; sonraki 512 bayt
    add ax, 0x20
    mov es, ax

    inc byte [sector]
    cmp byte [sector], 19       ; 18 sektor/track
    jb .advance_done
    mov byte [sector], 1
    inc byte [head]
    cmp byte [head], 2
    jb .advance_done
    mov byte [head], 0
    inc byte [cyl]

.advance_done:
    dec word [remaining]
    jnz .next_sector
    ret

.error:
    mov si, msg_disk_error
    call print_rm
    cli
    hlt
    jmp $

; -----------------------------------------------------------------------------
; A20: port 0x92'nin 1. biti. Reset bitini (0) set etmemeye dikkat.
; -----------------------------------------------------------------------------
a20_enable:
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

; -----------------------------------------------------------------------------
; print_rm: SI -> 0 ile biten string. BIOS teletype + COM1.
; -----------------------------------------------------------------------------
print_rm:
    push ax
.next:
    lodsb
    test al, al
    jz .done
    call putc_rm
    jmp .next
.done:
    pop ax
    ret

putc_rm:
    mov [char_rm], al

    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10

    mov dx, 0x3FD
.wait_tx:
    in al, dx
    test al, 0x20
    jz .wait_tx

    mov dx, 0x3F8
    mov al, [char_rm]
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

; =============================================================================
; 32-bit bolum
; =============================================================================
BITS 32

pm32:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; kernel.bin: 0x10000 -> 0x100000
    mov esi, KERNEL_LOAD_SEG * 16
    mov edi, KERNEL_ENTRY
    mov ecx, KERNEL_SECTORS * 128   ; 512 bayt = 128 dword
    cld
    rep movsd

    ; Framebuffer'i sifirla (0x00 ile doldur)
    mov edi, [BOOTINFO_ADDR+16]     ; physbase
    test edi, edi
    jz .skip_clear
    movzx eax, word [BOOTINFO_ADDR+26] ; pitch
    test eax, eax
    jnz .p_set
    mov eax, 800
.p_set:
    movzx ecx, word [BOOTINFO_ADDR+22] ; height
    test ecx, ecx
    jnz .h_set
    mov ecx, 600
.h_set:
    imul ecx, eax                   ; total bytes = pitch * height
    test ecx, ecx
    jz .skip_clear
    xor eax, eax
    cld
    rep stosb
.skip_clear:

    mov edi, 0xB8000
    mov esi, msg_pm_ready
    mov bl, 0x1E
    call pm_print

    jmp KERNEL_ENTRY

.hang:
    cli
    hlt
    jmp .hang

; -----------------------------------------------------------------------------
; pm_print: ESI -> string, EDI -> VGA konumu, BL -> renk. VGA + COM1.
; -----------------------------------------------------------------------------
pm_print:
    lodsb
    test al, al
    jz .done

    cmp al, 32
    jb .serial
    mov ah, bl
    stosw

.serial:
    push eax
    mov dx, 0x3FD
.wait_tx:
    in al, dx
    test al, 0x20
    jz .wait_tx
    pop eax
    mov dx, 0x3F8
    out dx, al

    jmp pm_print
.done:
    ret

; =============================================================================
; Veri
; =============================================================================
boot_drive:  db 0
char_rm:     db 0
sector:      db 0
head:        db 0
cyl:         db 0
remaining:   dw 0
e820_count:  dw 0

msg_loaded:      db "casrev-os v0.9 - stage2 diskten yuklendi.", 13, 10, 0
msg_memory:      db "E820 bellek haritasi alindi.", 13, 10, 0
msg_kernel:      db "Cekirdek diskten 0x10000 adresine yuklendi.", 13, 10, 0
msg_a20:         db "A20 hatti acildi.", 13, 10, 0
msg_pmode:       db "Protected mode: cekirdek 1 MB'a kopyalaniyor...", 13, 10, 0
msg_pm_ready:    db "Cekirdege atlaniyor...", 13, 10, 0
msg_disk_error:  db "HATA: cekirdek diskten okunamadi!", 13, 10, 0
msg_vesa_error:  db "HATA: VESA 800x600x16 modu bulunamadi veya etkinlestirilemedi!", 13, 10, 0

; -----------------------------------------------------------------------------
; GDT: null + flat kod + flat veri
; -----------------------------------------------------------------------------
align 8
gdt_start:
    dq 0x0000000000000000
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdtr:
    dw gdt_end - gdt_start - 1
    dd gdt_start
