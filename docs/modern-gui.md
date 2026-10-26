# casrev-os Modern Masaüstü — Tasarım Notları

Bu belge, casrev-os v0.9'daki 16-bit masaüstü mimarisini ve görsel katmanı özetler.

## Genel bakış

- **Çözünürlük:** VESA mode `0x114` (`0x4114`, LFB), 800×600, 16-bit
  RGB565, doğrusal framebuffer.
- **Pitch:** 1600 bayt (800 × 2); LFB adresi `boot_info_t` üzerinden gelir
  (QEMU'da `0xFD000000`).
- **Çizim:** `src/kernel/gfx.c` içinde 1024×768×`uint16_t` arka tampon
  (1,5 MB, `.bss`). Tüm UI çizimi burada yapılır; `gfx_flush()` yalnızca
  görünen 800×600 bölümü satır satır LFB'ye kopyalar.
- **UI:** `src/kernel/ui.c` içinde masaüstü, pencereler, görev çubuğu, Başlat
  menüsü, duvar kağıtları, yerel + internet Arama ve uygulama içerikleri.
- **Girdi:** PS/2 klavye (IRQ1) ve fare (IRQ12); `shell_run()` döngüsü
  `ui_poll()` + `keyboard_getchar()` ile olayları işler.
- **Yenileme:** UI yalnızca kirli bayrak set edildiğinde yeniden çizilir.

## 16-bit renk hattı

Eski 8-bit palet API'si korunmuştur; böylece uygulama çizimleri değişmeden
çalışır. Renkler tek bir LUT üzerinden RGB565'e çevrilir:

| Dizin | İçerik |
|-------|--------|
| 0–15 | Klasik VGA renkleri |
| 16–31 | Modern masaüstü UI renkleri (`UI_ACCENT`, `UI_CARD_BG`, ...) |
| 32–247 | 6×6×6 RGB renk küpü (legacy uyumluluk) |
| 248–255 | 8 kademeli gri |

- `legacy_lut[256]`, `gfx_init()` sırasında `pack_rgb()` ile bir kez kurulur.
- `gfx_pixel` / `gfx_fill_rect` gibi eski fonksiyonlar LUT üzerinden çizer.
- Yeni görsel katman doğrudan `gfx_rgb(r, g, b)` ve `GFX_RGB565()` kullanır.
- `pack_rgb()` kanalları 0–255 aralığına kırpar; RGB565'e 5-6-5 bit olarak
  paketler, `gfx_gradient_v()` ise karışım için kanalları tekrar 8 bite açar.
- 16-bit modda VGA DAC kullanılmaz; `gfx_set_palette()` yalnızca eski
  8-bit mod uyumluluğu için bırakılmıştır.

## Görsel yardımcılar

`gfx.c` içindeki tam renkli API:

- `gfx_blend(dst, src, alpha)` ve `gfx_blend_rect(...)` — 0–255 alfa ile
  kaynak-üstü karışım; görev çubuğu ve Başlat menüsünün yarı saydam cam
  görünümünü üretir.
- `gfx_gradient_v(...)` — dikey doğrusal gradyan; 8-bit'e açılmış kanallara
  `{0, 1, 0, -1}` dört satırlık dither ekleyerek RGB565 bantlanmasını azaltır.
- `gfx_fill_rounded_rect_gradient_v(...)` — yuvarlatılmış köşeli, dikey
  gradyanlı dolgu; pencere başlıkları ve Başlat butonu bunu kullanır.
- `gfx_shadow_soft(...)` — dokuz katmanlı, aşağı-sağa kaymış yumuşak gölge;
  her katmanda alfa artar, yuvarlatılmış dikdörtgen büyür.
- `gfx_fill_circle_blend(...)` — ay/güneş ışıması ve hâle efektleri için
  alfalı daire.
- `gfx_fill_rounded_rect_rgb(...)`, `gfx_rect_rounded_rgb(...)` — modern
  kart, buton ve pencere kenarlıkları.

## Pencere yönetimi

`window_t` yapısı şu durumları tutar:

```c
int x, y, w, h;
int is_visible, is_minimized, is_active;
int is_maximized;
int restore_x, restore_y, restore_w, restore_h;
```

- Başlık çubuğundan sürükleme.
- `[-]` küçültme, `[ ]` büyütme/geri yükleme, `[x]` kapatma.
- Başlık çubuğuna çift tıklama ile büyütme/geri yükleme.
- Tıklanan pencere en öne gelir; aktif pencere en son çizilir.
- Görev çubuğu sekmeleri görünür pencere sayısına göre dinamik genişlik alır;
  sekme tıklaması pencereyi küçültür/geri açar.

Pencere çerçevesi 5 px yuvarlatılır; aktif başlık mavi gradyanlı ve üst
ışıklı, pasif başlık gridir. Kapat düğmesi kırmızı, küçült/büyüt düğmeleri
koyu mavidir; üçü de fare hover'ında parlar. Büyütülen pencere masaüstü
alanını kaplar (`0,0,800,562`); görev çubuğu altta kalır.

## Masaüstü ve duvar kağıtları

`wallpaper_mode` dört temayı seçer. Her tema üç duraçlı (üst, orta, alt) bir
RGB gradyanıdır; iki yarım parça hâlinde `gfx_gradient_v()` ile çizilir:

| Değer | Tema | Renk duraçları (RGB) | İçerik |
|-------|------|----------------------|--------|
| 0 | Gece | `6,9,20` → `18,26,48` → `54,42,72` | Yıldızlar, hilal ay, şehir silueti |
| 1 | Okyanus | `3,15,30` → `8,56,82` → `0,104,132` | Mavi gradyan, yıldızlar, ay, siluet |
| 2 | Gün Batımı | `44,18,52` → `126,44,72` → `224,112,52` | Turuncu güneş hâlesi, sıcak siluet |
| 3 | Klasik | `10,13,22` (düz) | Sade koyu masaüstü |

- Ufuk çizgisinde 150 px yüksekliğinde düşük alfalı ışık bandı vardır.
- Yıldızlar 25 sabit noktadan oluşur; bazıları dört kollu ışıma yapar.
- Ay/güneş, iç içe alfalı dairelerle yumuşak hâle kazanır.
- Şehir silueti 18 farklı genişlik/yükseklikte binadan oluşur; bazı
  pencereler sarı, bazıları camgöbeği yanar; antenlerde kırmızı havacılık
  ışığı bulunur.
- Ayarlar penceresindeki önizlemelere tıklanınca tema anında değişir.

## Görev çubuğu ve Başlat menüsü

- Görev çubuğu 38 px yüksekliğindedir ve `alpha=228` koyu cam panel olarak
  çizilir; üstünde iki ince ışık/kontur çizgisi bulunur.
- Başlat butonu 84×30 px, dört karelik logo ve gradyan zemin kullanır;
  hover ve menü açık durumunda mavi vurgu alır.
- Açık pencereler için sekmeler: aktif sekme mavi kenarlıklı ve üst ışıklı,
  hover'da açılır, pasifte koyu kalır.
- Başlat menüsü `alpha=238` yarı saydam panel, yumuşak gölge, gradyan başlık
  ve hover'da vurgulanan dokuz uygulama satırı içerir.
- Sistem tepsisi canlı PMM RAM kullanımını (`RAM: X/31M`), fare koordinatlarını
  ve RTC saatini gösterir.

## Uygulamalar

1. Terminal — kabuk çıktısını ve komut geçmişini gösterir.
2. Dosya Gezgini — RamFS üzerinde gezinme, dosya açma, yeni not.
3. Not Defteri — metin düzenleme, kaydetme, temizleme; Türkçe glif desteği.
4. Paint — renk paleti, üç fırça boyutu, silgi.
5. Mayın Tarlası — 9×9 oyun tahtası ve bayrak koyma.
6. Hesap Makinesi — fare ve klavye ile dört işlem.
7. Sistem Bilgisi — tick, uptime, RTC, RAM çubuğu.
8. Arama — Google görünümlü; yerel kaynaklar ve anonim internet sonuçları.
9. Ayarlar — duvar kağıdı, klavye bilgisi, kısayollar, canlı sistem durumu.

## Arama (yerel + internet)

Arama uygulaması, RamFS ve çekirdek içi tabloları tarayan yerel arama ile
DuckDuckGo üzerinden gelen anonim web sonuçlarını aynı listede birleştirir.
Pencere `140,50` konumunda `520×420` boyutundadır; başlıkta renkli `casrev`
logosu, altında arama alanı ve `Ara` butonu bulunur.

- **Yerel kaynaklar (öncelik sırasıyla):**
  1. Uygulama adı ve açıklaması (Terminal, Ayarlar, Not Defteri, ...).
  2. RamFS dosyaları: dosya adı, tam yol ve dosya içeriği.
  3. Kabuk komutları: komut adı ve açıklaması (`help`, `settings`, `arama`, ...).
- **Eşleştirme:** `search_fold()` Türkçe karakterleri ASCII karşılıklarına
  katlar ve büyük/küçük harf duyarsız alt dizi araması yapar. Böylece `gorev`,
  `Görev` ve `GOREV` aynı sonucu bulur.
- **Birleştirme:** Toplam en fazla beş satır gösterilir. Web sonucu varsa
  en fazla üç yerel + iki web satırı, yoksa beş yerel satır gösterilir. Web
  satırları mavi `W` ikonu ve `Internet | site` meta bilgisiyle ayrılır.
- **Boş sorgu:** Hızlı erişim kartları olarak ilk beş uygulama listelenir.
- **Eylemler:** Dosya sonucu Not Defterinde açılır; uygulama sonucu ilgili
  pencereyi öne getirir; komut sonucu Terminali etkinleştirir. Web sonucu
  `WEB SONUCU` kartını açar: başlık, site, URL, özet ve `Geri` düğmesi.
  `Enter` ilk sonucu açar, `Backspace` sorguyu düzenler; detay kartı açıkken
  `Enter` veya `Geri` kapatır.
- **Durum çubuğu:** Solda toplam/yerel/web sonuç sayısı, sağda
  `Internet: hazir`, `Internet: araniyor`, `Internet: sonuc yok`,
  `Internet: hata` veya `Internet: yok` durumu renkli gösterilir.
- **Çevrimdışı davranış:** COM2 yoksa veya ajan yanıt vermezse yerel arama
  aynen çalışır; beş yerel sonuç gösterilir. Web satırları yalnızca başarılı
  yanıttan sonra listeye eklenir.
- **Kabuktan:** `search [sorgu]` veya `arama [sorgu]` komutu pencereyi isteğe
  bağlı hazır sorguyla açar.

## İnternet arama köprüsü

Misafir işletim sisteminde NIC sürücüsü, ARP/IP, DNS, TCP veya TLS yoktur.
Bu adımda gerçek internet erişimi host tarafındaki küçük bir Python ajanına
devredilir; misafir yalnızca COM2 seri hattını kullanır.

```
casrev-os (guest)                    host
┌──────────────────┐   COM2 TCP     ┌──────────────────────────┐
│ ui.c search_run  │ 127.0.0.1:5568 │ scripts/search-agent.py  │
│ serial2_write  ──┼───────────────►│ DuckDuckGo HTML          │
│ serial2_read   ◄─┼────────────────│ Wikipedia API (yedek)    │
└──────────────────┘   Q/R/T/END    └──────────────────────────┘
```

- **QEMU:** `scripts/run.ps1` ajanı gizli pencerede başlatır ve ikinci seri
  portu `-serial tcp:127.0.0.1:5568,server=on,wait=off` olarak ekler. COM1
  (`0x3F8`) log/stdio için, COM2 (`0x2F8`) arama köprüsü için ayrılmıştır.
- **Protokol:** Sekmeyle ayrılmış, CP1254 kodlu satırlar:

  | Yön | Satır | Anlam |
  |-----|-------|-------|
  | Misafir → host | `Q\t<id>\t<sorgu>\n` | Yeni arama isteği |
  | Host → misafir | `R\t<id>\t<sayı>\n` | Sonuç sayısı |
  | Host → misafir | `T\t<başlık>\t<site>\t<URL>\t<özet>\n` | Bir web sonucu |
  | Host → misafir | `END\t<id>\n` | Yanıt bitti |
  | Host → misafir | `E\t<id>\t<mesaj>\n` | Arama hatası |

- **Zamanlama:** Tuş vuruşları 400 ms debounce edilir; yanıt 8 saniye içinde
  gelmezse UI `Internet: hata` durumuna geçer ve yerel sonuçlara döner.
- **Ajan:** DuckDuckGo `html.duckduckgo.com/html/?q=...` sayfasını ayrıştırır;
  yeterli sonuç yoksa Türkçe ve İngilizce Wikipedia API'sine düşer. Hesap,
  API anahtarı ve çerez kullanmaz. Başlık 76, site 36, URL 156, özet 136
  karaktere sınırlanır; sekmeler ve kontrol karakterleri temizlenir.
- **Gizlilik:** Sorgu yalnızca host üzerinden arama motoruna gider; COM2
  bağlantısı `127.0.0.1` ile sınırlıdır. Ajan kalıcı kimlik veya arama geçmişi
  tutmaz.
- **Sınırlar:** Web sonucu sayfası tarayıcıda açılmaz; yalnızca başlık, site,
  URL ve özet gösterilir. Gerçek sayfa çizimi, native NIC/TCP/TLS yığını ve
  kalıcı çerezler bu sürümün kapsamı dışındadır.
- **Kapalı mod:** `scripts/run.ps1 -NoInternet` ajanı ve COM2'yi devre dışı
  bırakır; uygulama yalnızca yerel aramayla çalışır.

## Türkçe-Q klavye

`src/kernel/keyboard.c` scancode set 1'i CP1254 kodlarına eşler:

- `[` → ğ, `]` → ü, `;` → ş, `'` → i, `,` → ö, `.` → ç, `i` → ı.
- Shift ile büyük harfler; Türkçe'ye özgü `i → İ`, `ı → I`.
- AltGr (sağ Alt) ile `@ # $ { } [ ] | \ ~ < >`.
- `gfx.c` bu kodlar için 8×8 özel glifler sağlar.
- Kabuk, komutları karşılaştırmadan önce Türkçe karakterleri ASCII'ye
  normalize eder.

## Ayarlar yerleşimi

- Normal boyutta (380×330): duvar kağıdı önizlemeleri, klavye kartı ve sistem
  kartı.
- Büyütülmüş boyutta (800×562): büyük duvar kağıdı kartları, yan yana klavye
  ve masaüstü kartları, kısayollar kartı ve canlı sistem durumu (uptime, RAM,
  tarih/saat, çözünürlük, fare).

## Test kapsamı

- `scripts/test.ps1`: açılış, PMM, sayfalama, heap; 16-bit video bilgisi.
- `scripts/test-shell.ps1`: kabuk komutları, Türkçe tuş eşlemesi, fare IRQ12.
- `scripts/test-gui.ps1`: masaüstü ve Not Defteri ekran görüntüsü.
- `scripts/test-settings.ps1`: Ayarlar simgesi, duvar kağıdı seçimi, büyütme,
  çift tıklama ile geri yükleme, küçültme, görev çubuğu, Başlat menüsü ve
  hover'ı, sekiz uygulama açıkken sekme yerleşimi ve Türkçe karakter yazımı.
- `scripts/test-search.ps1`: masaüstü Arama simgesi, boş sorgu hızlı erişimi,
  `gorev` ile RamFS dosyası ve Not Defteri, `ayar` ile Ayarlar uygulaması,
  `help` ile kabuk komutu sonucu ve Terminalin öne gelmesi.
- `scripts/test-search-online.ps1`: Python ajanını başlatır, QEMU'ya COM2
  ekler, gerçek `Linux` sorgusunu DuckDuckGo üzerinden çalıştırır; web
  satırlarını, `WEB SONUCU` detay kartını ve `Geri` düğmesini doğrular.
  Ardından `ayar` sorgusuyla üç yerel + iki web satırını aynı listede gösterip
  birleşik görünümü ve web detay kartını kaydeder.
- Ekran görüntüleri `build/settings-*.png`, `build/search-*.png`,
  `build/search-online-*.png`, `build/gui.png` ve belge görselleri
  `docs/stage10.png`, `docs/arama.png` ile `docs/arama-online.png`
  olarak saklanır; QEMU `screendump` çıktısı `build/ppm2png.py` ile PNG'ye
  çevrilir.

## Gelecek çalışmalar

1. ATA PIO + FAT12 ile kalıcı dosyalar.
2. Süreçler, görev değiştirme ve sistem çağrıları.
3. Native NIC + TCP/IP + TLS yığını ve tam sayfa tarayıcı.
4. Daha fazla uygulama ve oyun.
5. Animasyonlar, pencere geçişleri ve kullanıcı tema desteği.
6. Kararlılık/performans ölçümleri ve optimizasyonlar.
