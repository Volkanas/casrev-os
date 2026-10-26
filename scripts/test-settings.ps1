# =============================================================================
# casrev-os - Ayarlar / pencere yonetimi / Turkce klavye uctan uca testi
#   QEMU monitor uzerinden gercek fare tikleri ve tus basislari gonderir,
#   her adimda ekran goruntusu alir.
# =============================================================================
param(
    [int]$BootWaitSec = 3,
    [int]$MonitorPort = 5566
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$img  = Join-Path $root 'build\casrev-os.img'
$log  = Join-Path $root 'build\serial-settings.log'
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'

if (-not (Test-Path $img)) { & (Join-Path $PSScriptRoot 'build.ps1') }
if (-not (Test-Path $qemu)) {
    $cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source } else { throw 'QEMU bulunamadi.' }
}

$shots = @(
    'settings-desktop',
    'settings-open',
    'settings-okyanus',
    'settings-sunset',
    'settings-klasik',
    'settings-max',
    'settings-restored',
    'settings-min',
    'settings-taskbar',
    'settings-closed',
    'settings-startmenu',
    'settings-startmenu-hover',
    'settings-reopen',
    'settings-notepad',
    'settings-trkeys',
    'settings-allapps'
)

foreach ($name in $shots) {
    Remove-Item (Join-Path $root "build\$name.ppm") -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $root "build\$name.png") -ErrorAction SilentlyContinue
}
Remove-Item $log -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $qemu `
    -ArgumentList '-drive', 'format=raw,if=floppy,file=build/casrev-os.img',
                  '-boot', 'a', '-m', '32M', '-display', 'none',
                  '-serial', 'file:build/serial-settings.log',
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

    function Double-Click-At([int]$x, [int]$y) {
        Move-To $x $y
        Send-Cmd 'mouse_button 1'
        Send-Cmd 'mouse_button 0'
        Start-Sleep -Milliseconds 80
        Send-Cmd 'mouse_button 1'
        Send-Cmd 'mouse_button 0'
        Start-Sleep -Milliseconds 450
    }

    function Screenshot([string]$name) {
        Send-Cmd "screendump build/$name.ppm"
        Start-Sleep -Milliseconds 700
    }

    # Turkce karakterleri betik kodlamasindan bagimsiz tanimla (CP1254/Unicode).
    $tr_lower_u = [char]0x00FC   # u diaeresis
    $tr_upper_u = [char]0x00DC
    $tr_lower_g = [char]0x011F   # g breve
    $tr_upper_g = [char]0x011E
    $tr_lower_s = [char]0x015F   # s cedilla
    $tr_upper_s = [char]0x015E
    $tr_lower_o = [char]0x00F6   # o diaeresis
    $tr_upper_o = [char]0x00D6
    $tr_lower_c = [char]0x00E7   # c cedilla
    $tr_upper_c = [char]0x00C7
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
                $tr_upper_u     { 'shift-bracket_right' }
                $tr_lower_g     { 'bracket_left' }
                $tr_upper_g     { 'shift-bracket_left' }
                $tr_lower_s     { 'semicolon' }
                $tr_upper_s     { 'shift-semicolon' }
                $tr_lower_o     { 'comma' }
                $tr_upper_o     { 'shift-comma' }
                $tr_lower_c     { 'dot' }
                $tr_upper_c     { 'shift-dot' }
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

    Screenshot 'settings-desktop'

    # Masaustu Ayarlar simgesi: (18..66, 478..538)
    Click-At 42 500
    Screenshot 'settings-open'

    # Tum duvar kagidi onizlemeleri (pencere 300,80 -> kartlar 312..668, 156..222)
    Click-At 444 180
    Screenshot 'settings-okyanus'
    Click-At 535 180
    Screenshot 'settings-sunset'
    Click-At 626 180
    Screenshot 'settings-klasik'

    # Testin devami icin Gun Batimi temasina don
    Click-At 535 180

    # Buyut dugmesi (sag ustteki ucuncu dugme)
    Click-At 631 92
    Screenshot 'settings-max'

    # Baslik cubuguna cift tik: geri yukle
    Double-Click-At 400 12
    Screenshot 'settings-restored'

    # Kucult dugmesi
    Click-At 649 92
    Screenshot 'settings-min'

    # Gorev cubugundaki "Ayar" sekmesine tikla: geri yukle
    Click-At 206 580
    Screenshot 'settings-taskbar'

    # Kapat (kapat dugmesi: 660..674, 85..99)
    Click-At 667 92
    Screenshot 'settings-closed'

    # Baslat menusu ve icinden Ayarlar
    Click-At 50 580
    Screenshot 'settings-startmenu'
    Move-To 100 470
    Screenshot 'settings-startmenu-hover'
    Click-At 100 470
    Screenshot 'settings-reopen'

    # Kapat, terminale tikla, Not Defterini ac
    Click-At 667 92
    Click-At 350 200
    Send-Text 'notepad readme.txt'
    Send-Key 'ret'
    Start-Sleep -Milliseconds 700
    Screenshot 'settings-notepad'

    # Temizle (454..528, 90..114), sonra Turkce karakterleri yaz
    Click-At 491 102
    $turkish_text = "T${tr_lower_u}rk${tr_lower_c}e: " +
                    "${tr_lower_g}${tr_lower_u}${tr_lower_s}i${tr_lower_o}${tr_lower_c} " +
                    "${tr_dotted_upper}${tr_dotless} " +
                    "${tr_upper_g}${tr_upper_u}${tr_upper_s}${tr_upper_o}${tr_upper_c}"
    Send-Text $turkish_text
    Screenshot 'settings-trkeys'

    # Not Defterini kapat, terminali etkinlestir ve diger uygulamalari ac.
    # Not: Hesap Makinesi etkinken Enter'i kendisi tuketir; bu yuzden onu
    # son kabuk komutu olarak aciyoruz. Not Defterini fare ile aciyoruz.
    Click-At 627 72
    Click-At 350 200
    foreach ($cmd in @('explorer', 'paint', 'mines', 'sysinfo', 'settings', 'calc')) {
        Send-Text $cmd
        Send-Key 'ret'
        Start-Sleep -Milliseconds 650
    }

    # Masaustu Notlar simgesi: (18..66, 148..198)
    Click-At 42 160

    # Not Defterini gorev cubugundan kucult: sekiz sekme ayni anda gorunsun
    Click-At 341 580
    Screenshot 'settings-allapps'

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
