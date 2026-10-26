#include "shell.h"
#include "console.h"
#include "heap.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "paging.h"
#include "pmm.h"
#include "rtc.h"
#include "string.h"
#include "timer.h"
#include "types.h"
#include "ui.h"
#include "vfs.h"
#include "vga.h"

#define LINE_MAX 128

static char line[LINE_MAX];
static uint32_t length;

static void prompt(void)
{
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("casrev> ");
}

/*
 * Turkce karakterleri kabuk komutlarinda ASCII karsiliklarina cevirir.
 * Boylece Turkce Q klavyesiyle yazilan komutlar da calisir.
 */
static char tr_to_ascii(char c)
{
    switch ((unsigned char)c) {
    case 0xC7: case 0xE7: return 'c';
    case 0xD0: case 0xF0: return 'g';
    case 0xD6: case 0xF6: return 'o';
    case 0xDC: case 0xFC: return 'u';
    case 0xDD: case 0xFD: return 'i';
    case 0xDE: case 0xFE: return 's';
    default: return c;
    }
}

static void command_help(void)
{
    kprintf("Komutlar:\n");
    kprintf("  help      Bu listeyi gosterir\n");
    kprintf("  clear     Ekrani temizler\n");
    kprintf("  uptime    Calisma suresini gosterir\n");
    kprintf("  echo ...  Metni ekrana yazar\n");
    kprintf("  mem       Bellek istatistikleri\n");
    kprintf("  frame     Kare ayir, yaz, geri ver\n");
    kprintf("  heap      kmalloc/kfree testi\n");
    kprintf("  time      RTC tarih/saat bilgisi\n");
    kprintf("  mouse     Fare konumu ve dugmeleri\n");
    kprintf("  ls [yol]  Dizindeki dosyalari listeler\n");
    kprintf("  cd <yol>  Dizini degistirir\n");
    kprintf("  pwd       Gecerli dizini yazdirir\n");
    kprintf("  cat <f>   Dosya icerigini okur\n");
    kprintf("  mkdir <d> Yeni klasor olusturur\n");
    kprintf("  touch <f> Yeni dosya olusturur\n");
    kprintf("  explorer  Dosya Gezginini acar\n");
    kprintf("  notepad   Not Defterini acar (notepad <dosya>)\n");
    kprintf("  paint     Paint cizim tahtasini acar\n");
    kprintf("  mines     Mayin Tarlasi oyununu acar\n");
    kprintf("  calc      Hesap Makinesi penceresini acar\n");
    kprintf("  sysinfo   Sistem Bilgisi penceresini acar\n");
    kprintf("  settings  Ayarlar penceresini acar\n");
    kprintf("  about     Sistem bilgisi\n");
    kprintf("  launcher  Baslat menusunu acar/kapatir (ESC)\n");
    kprintf("  reboot    Sistemi yeniden baslatir\n");
    kprintf("  halt      Sistemi durdurur\n");
}

static void execute(void)
{
    line[length] = '\0';

    char normalized[LINE_MAX];
    for (uint32_t i = 0; i < length; i++) {
        normalized[i] = tr_to_ascii(line[i]);
    }
    normalized[length] = '\0';

    char *cmd = normalized;
    while (*cmd == ' ') {
        cmd++;
    }

    char *arg = cmd;
    while (*arg && *arg != ' ') {
        arg++;
    }
    if (*arg) {
        *arg++ = '\0';
        while (*arg == ' ') {
            arg++;
        }
    }

    if (*cmd == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        command_help();
    } else if (strcmp(cmd, "clear") == 0) {
        vga_clear();
    } else if (strcmp(cmd, "uptime") == 0) {
        kprintf("Calisma suresi: %u.%u saniye (%u tick)\n",
                timer_ticks() / TIMER_HZ,
                (timer_ticks() % TIMER_HZ) / 10,
                timer_ticks());
    } else if (strcmp(cmd, "echo") == 0) {
        kprintf("%s\n", arg);
    } else if (strcmp(cmd, "mem") == 0) {
        kprintf("Fiziksel bellek: %u MB (%u kare)\n",
                (uint32_t)(pmm_total_bytes() / (1024 * 1024)),
                pmm_total_frames());
        kprintf("  bos kare: %u, kullanilan: %u\n",
                pmm_free_frames(), pmm_used_frames());
        kprintf("Sayfalama: %s, %u MB eslendi\n",
                paging_enabled() ? "ACIK" : "KAPALI",
                paging_mapped_bytes() / (1024 * 1024));
        kprintf("Heap: %u/%u bayt kullaniliyor\n",
                heap_used_bytes(), heap_total_bytes());
    } else if (strcmp(cmd, "frame") == 0) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == 0) {
            kprintf("Bos fiziksel kare yok!\n");
        } else {
            kprintf("Kare ayrildi: 0x%X, bos kalan: %u\n",
                    frame, pmm_free_frames());

            volatile uint32_t *probe = (volatile uint32_t *)frame;
            *probe = 0xDEADBEEF;
            kprintf("Kareye yazma/okuma: %s\n",
                    (*probe == 0xDEADBEEF) ? "OK" : "HATA");

            pmm_free_frame(frame);
            kprintf("Kare geri verildi: 0x%X\n", frame);
        }
    } else if (strcmp(cmd, "heap") == 0) {
        void *a = kmalloc(32);
        void *b = kmalloc(512);
        void *c = kmalloc(2048);

        if (!a || !b || !c) {
            kprintf("kmalloc basarisiz!\n");
        } else {
            kprintf("kmalloc: %p %p %p\n", a, b, c);
            kfree(b);
            kfree(a);
            kfree(c);
            kprintf("kfree sonrasi kullanilan: %u bayt\n",
                    heap_used_bytes());
        }
    } else if (strcmp(cmd, "time") == 0) {
        char clock[9];
        char date[11];
        rtc_format_time(clock);
        rtc_format_date(date);
        kprintf("Saat: %s  Tarih: %s\n", clock, date);
    } else if (strcmp(cmd, "mouse") == 0) {
        int mx;
        int my;
        int buttons;
        mouse_get_state(&mx, &my, &buttons);
        kprintf("Fare: x=%d y=%d dugmeler=%u\n", mx, my, (uint32_t)buttons);
    } else if (strcmp(cmd, "ls") == 0) {
        vfs_node_t *dir = (*arg) ? vfs_resolve_path(arg) : vfs_get_cwd();
        if (!dir || dir->type != VFS_TYPE_DIR) {
            kprintf("Dizin bulunamadi: %s\n", arg);
        } else {
            vfs_node_t *c = dir->first_child;
            if (!c) {
                kprintf("Dizin bos.\n");
            }
            while (c) {
                if (c->type == VFS_TYPE_DIR) {
                    vga_set_color(VGA_YELLOW, VGA_BLACK);
                    kprintf("  [DIR]  %s/\n", c->name);
                } else {
                    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                    kprintf("  [FILE] %s  (%u B)\n", c->name, c->size);
                }
                c = c->next_sibling;
            }
            vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        }
    } else if (strcmp(cmd, "pwd") == 0) {
        char pbuf[128];
        vfs_get_path(vfs_get_cwd(), pbuf, sizeof(pbuf));
        kprintf("%s\n", pbuf);
    } else if (strcmp(cmd, "cd") == 0) {
        if (!*arg) {
            vfs_set_cwd(vfs_get_root());
        } else {
            vfs_node_t *target = vfs_resolve_path(arg);
            if (!target || target->type != VFS_TYPE_DIR) {
                kprintf("Dizin bulunamadi: %s\n", arg);
            } else {
                vfs_set_cwd(target);
            }
        }
    } else if (strcmp(cmd, "cat") == 0) {
        if (!*arg) {
            kprintf("Kullanim: cat <dosya>\n");
        } else {
            vfs_node_t *f = vfs_resolve_path(arg);
            if (!f || f->type != VFS_TYPE_FILE) {
                kprintf("Dosya bulunamadi: %s\n", arg);
            } else {
                kprintf("%s", f->content ? f->content : "");
                if (f->size > 0 && f->content && f->content[f->size - 1] != '\n') {
                    kprintf("\n");
                }
            }
        }
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (!*arg) {
            kprintf("Kullanim: mkdir <klasor>\n");
        } else {
            vfs_create_dir(vfs_get_cwd(), arg);
            kprintf("Klasor olusturuldu: %s\n", arg);
        }
    } else if (strcmp(cmd, "touch") == 0) {
        if (!*arg) {
            kprintf("Kullanim: touch <dosya>\n");
        } else {
            vfs_create_file(vfs_get_cwd(), arg, "");
            kprintf("Dosya olusturuldu: %s\n", arg);
        }
    } else if (strcmp(cmd, "explorer") == 0) {
        ui_window_open(UI_WIN_EXPLORER);
        kprintf("Dosya Gezgini acildi.\n");
    } else if (strcmp(cmd, "notepad") == 0) {
        if (*arg) {
            ui_notepad_open_file(arg);
        }
        ui_window_open(UI_WIN_NOTEPAD);
        kprintf("Not Defteri acildi.\n");
    } else if (strcmp(cmd, "paint") == 0) {
        ui_window_open(UI_WIN_PAINT);
        kprintf("Paint cizim tahtasi acildi.\n");
    } else if (strcmp(cmd, "mines") == 0) {
        ui_window_open(UI_WIN_MINES);
        kprintf("Mayin Tarlasi oyunu acildi.\n");
    } else if (strcmp(cmd, "calc") == 0) {
        ui_window_open(UI_WIN_CALC);
        kprintf("Hesap Makinesi penceresi acildi.\n");
    } else if (strcmp(cmd, "sysinfo") == 0) {
        ui_window_open(UI_WIN_SYSINFO);
        kprintf("Sistem Bilgisi penceresi acildi.\n");
    } else if (strcmp(cmd, "settings") == 0 || strcmp(cmd, "ayarlar") == 0) {
        ui_window_open(UI_WIN_SETTINGS);
        kprintf("Ayarlar penceresi acildi.\n");
    } else if (strcmp(cmd, "about") == 0) {
        ui_window_open(UI_WIN_SYSINFO);
        kprintf("casrev-os v0.9 - 32-bit x86\n");
        kprintf("Modern Masaustu Arayuzu, VESA 800x600\n");
        kprintf("COM1, IDT/PIC, PIT 100 Hz, PS/2 Klavye & Fare, RTC\n");
        kprintf("E820, PMM, 4 KB Sayfalama, kmalloc Heap\n");
    } else if (strcmp(cmd, "launcher") == 0) {
        ui_start_menu_toggle();
        kprintf("Baslat menusu acildi/kapandi (ESC).\n");
    } else if (strcmp(cmd, "reboot") == 0) {
        kprintf("Yeniden baslatiliyor...\n");
        outb(0x64, 0xFE);               /* 8042 reset */
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    } else if (strcmp(cmd, "halt") == 0) {
        kprintf("Sistem durduruldu. QEMU penceresini kapatabilirsiniz.\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    } else {
        kprintf("Bilinmeyen komut: %s  (help yazin)\n", cmd);
    }
}

void shell_init(void)
{
    length = 0;
    prompt();
}

void shell_run(void)
{
    for (;;) {
        ui_poll();

        int c = keyboard_getchar();

        if (c < 0) {
            __asm__ volatile ("sti; hlt");
            continue;
        }

        if (ui_handle_key(c)) {
            continue;
        }

        if (c == '\n') {
            console_putc('\n');
            execute();
            length = 0;
            prompt();
        } else if (c == '\b') {
            if (length > 0) {
                length--;
                kprintf("\b \b");
            }
        } else if ((c >= 32 && c < 127) || (c >= 0x80 && c <= 0xFF)) {
            if (length < LINE_MAX - 1) {
                line[length++] = (char)c;
                console_putc((char)c);
            }
        }
    }
}
