# =============================================================================
# casrev-os - yerel arama uygulamasi uctan uca testi
#   Masaustu simgesinden Arama'yi acar, RamFS dosyasi, uygulama ve kabuk
#   komutu sonuclarini gercek fare/klavye olaylariyla dogrular.
# =============================================================================
param(
    [int]$BootWaitSec = 3,
    [int]$MonitorPort = 5567
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$img  = Join-Path $root 'build\casrev-os.img'
$log  = Join-Path $root 'build\serial-search.log'
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'

if (-not (Test-Path $img)) { & (Join-Path $PSScriptRoot 'build.ps1') }
if (-not (Test-Path $qemu)) {
    $cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source } else { throw 'QEMU bulunamadi.' }
}

$shots = @(
    'search-desktop',
    'search-open',
    'search-gorev',
    'search-notepad',
    'search-ready',
    'search-ayar',
    'search-settings',
    'search-help',
    'search-terminal',
    'search-commandline'
)

foreach ($name in $shots) {
    Remove-Item (Join-Path $root "build\$name.ppm") -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $root "build\$name.png") -ErrorAction SilentlyContinue
}
Remove-Item $log -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $qemu `
    -ArgumentList '-drive', 'format=raw,if=floppy,file=build/casrev-os.img',
                  '-boot', 'a', '-m', '32M', '-display', 'none',
                  '-serial', 'file:build/serial-search.log',
                  '-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait" `
    -WorkingDirectory $root -PassThru -WindowStyle Hidden

try {
    Start-Sleep -Seconds $BootWaitSec
    $client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', $MonitorPort)
    $writer = New-Object System.IO.StreamWriter($client.GetStream())
    $writer.AutoFlush = $true

    function Send-Cmd([string]$cmd) {
        $writer.WriteLine($cmd)
        Start-Sleep -Milliseconds 120
    }

    function Send-Key([string]$key) {
        $writer.WriteLine("sendkey $key")
        Start-Sleep -Milliseconds 90
    }

    function Move-Home {
        for ($i = 0; $i -lt 4; $i++) {
            Send-Cmd 'mouse_move -300 -300'
        }
        Start-Sleep -Milliseconds 250
    }

    function Move-To([int]$x, [int]$y) {
        Move-Home
        Send-Cmd "mouse_move $x $y"
        Start-Sleep -Milliseconds 300
    }

    function Click-At([int]$x, [int]$y) {
        Move-To $x $y
        Send-Cmd 'mouse_button 1'
        Start-Sleep -Milliseconds 100
        Send-Cmd 'mouse_button 0'
        Start-Sleep -Milliseconds 400
    }

    function Screenshot([string]$name) {
        Send-Cmd "screendump build/$name.ppm"
        Start-Sleep -Milliseconds 700
    }

    $tr_lower_u = [char]0x00FC
    $tr_lower_g = [char]0x011F
    $tr_lower_s = [char]0x015F
    $tr_lower_o = [char]0x00F6
    $tr_lower_c = [char]0x00E7
    $tr_dotless = [char]0x0131
    $tr_dotted_upper = [char]0x0130

    function Send-Text([string]$text) {
        foreach ($ch in $text.ToCharArray()) {
            $key = switch -CaseSensitive ($ch) {
                ' '             { 'spc' }
                ':'             { 'shift-slash' }
                '.'             { 'slash' }
                ','             { 'backslash' }
                '-'             { 'minus' }
                '='             { 'equal' }
                '\'             { 'grave_accent' }
                'i'             { 'apostrophe' }
                $tr_lower_u     { 'bracket_right' }
                $tr_lower_g     { 'bracket_left' }
                $tr_lower_s     { 'semicolon' }
                $tr_lower_o     { 'comma' }
                $tr_lower_c     { 'dot' }
                $tr_dotless     { 'i' }
                $tr_dotted_upper { 'shift-apostrophe' }
                default {
                    if ($ch -cmatch '^[A-Z]$') { 'shift-' + ([string]$ch).ToLowerInvariant() }
                    else { [string]$ch }
                }
            }
            Send-Key $key
        }
    }

    Screenshot 'search-desktop'

    # Masaustu Arama simgesi: (18..66, 432..476)
    Click-At 42 452
    Screenshot 'search-open'

    # RamFS dosya aramasi: belgeler/gorevler.txt
    Send-Text 'gorev'
    Start-Sleep -Milliseconds 500
    Screenshot 'search-gorev'

    # Ilk sonucu ac: Not Defteri gorevler.txt icerigiyle acilmali
    Click-At 300 200
    Start-Sleep -Milliseconds 700
    Screenshot 'search-notepad'

    # Not Defterini kapat, aramayi gorev cubugundan one getir
    Click-At 627 72
    Click-At 200 580
    Screenshot 'search-ready'

    # Sorguyu temizle ve uygulama aramasi yap: Ayarlar
    for ($i = 0; $i -lt 5; $i++) { Send-Key 'backspace' }
    Send-Text 'ayar'
    Start-Sleep -Milliseconds 500
    Screenshot 'search-ayar'

    # Uygulama sonucunu ac
    Click-At 300 200
    Start-Sleep -Milliseconds 600
    Screenshot 'search-settings'

    # Ayarlari kapat, aramaya don ve kabuk komutu ara
    Click-At 667 92
    Click-At 200 580
    for ($i = 0; $i -lt 4; $i++) { Send-Key 'backspace' }
    Send-Text 'help'
    Start-Sleep -Milliseconds 500
    Screenshot 'search-help'

    # Komut sonucu Terminali one getirmeli
    Click-At 300 200
    Start-Sleep -Milliseconds 600
    Screenshot 'search-terminal'

    # Kabuk komutu: search <sorgu> pencereyi hazir sorguyla acmali
    Send-Text 'search ayar'
    Send-Key 'ret'
    Start-Sleep -Milliseconds 800
    Screenshot 'search-commandline'

    Send-Cmd 'quit'
    Start-Sleep -Milliseconds 500
    $client.Close()
} finally {
    Start-Sleep -Milliseconds 300
    if (-not $p.HasExited) { $p.Kill() }
}

foreach ($name in $shots) {
    $ppm = Join-Path $root "build\$name.ppm"
    $png = Join-Path $root "build\$name.png"
    if (Test-Path $ppm) {
        python (Join-Path $root 'build\ppm2png.py') $ppm $png
        Write-Host "Ekran goruntusu: $png"
    } else {
        Write-Host "[UYARI] Ekran goruntusu alinamadi: $name"
    }
}

if (Test-Path $log) {
    Write-Host '--- SERI CIKTI (son 6 satir) ---'
    Get-Content $log -Tail 6 | Write-Host
    Write-Host '--------------------------------'
}
