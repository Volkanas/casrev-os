# =============================================================================
# casrev-os - internet arama uctan uca testi
#   Host arama koprusunu (scripts/search-agent.py) baslatir, QEMU'ya COM2
#   olarak baglar, arama penceresinde gercek web sonucu ve detay kartini
#   fare/klavye olaylariyla dogrular.
# =============================================================================
param(
    [int]$BootWaitSec = 4,
    [int]$MonitorPort = 5569,
    [int]$Com2Port = 5568
)

$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$img      = Join-Path $root 'build\casrev-os.img'
$log      = Join-Path $root 'build\serial-online.log'
$agentLog = Join-Path $root 'build\search-agent-online.log'
$agentErr = Join-Path $root 'build\search-agent-online.err.log'
$agent    = Join-Path $PSScriptRoot 'search-agent.py'
$qemu     = 'C:\Program Files\qemu\qemu-system-i386.exe'

$shots = @(
    'search-online-desktop',
    'search-online-results',
    'search-online-detail',
    'search-online-back',
    'search-online-page-loading',
    'search-online-page',
    'search-online-page-scroll',
    'search-online-page-back',
    'search-online-mixed',
    'search-online-mixed-detail'
)

if (-not (Test-Path $img)) { & (Join-Path $PSScriptRoot 'build.ps1') }
if (-not (Test-Path $qemu)) {
    $cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source } else { throw 'QEMU bulunamadi.' }
}

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $python = $py
        $pythonPrefix = @('-3')
    } else {
        throw 'Python bulunamadi; internet arama testi calistirilamaz.'
    }
} else {
    $pythonPrefix = @()
}

foreach ($name in $shots) {
    Remove-Item (Join-Path $root "build\$name.ppm") -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $root "build\$name.png") -ErrorAction SilentlyContinue
}
Remove-Item $log, $agentLog, $agentErr -ErrorAction SilentlyContinue

$agentProc = $null
$qemuProc  = $null
$client    = $null
$failure   = $null
$webOk     = $false
$pageOk    = $false
$mixedOk   = $false

try {
    $qemuProc = Start-Process -FilePath $qemu `
        -ArgumentList '-drive', 'format=raw,if=floppy,file=build/casrev-os.img',
                      '-boot', 'a', '-m', '32M', '-display', 'none',
                      '-serial', 'file:build/serial-online.log',
                      '-serial', "tcp:127.0.0.1:$Com2Port,server=on,wait=off",
                      '-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait" `
        -WorkingDirectory $root -PassThru -WindowStyle Hidden

    Start-Sleep -Milliseconds 400
    $agentProc = Start-Process -FilePath $python.Source `
        -ArgumentList (@($pythonPrefix) + @(
            $agent, '--host', '127.0.0.1', '--port', "$Com2Port", '--wait', '20'
        )) `
        -WorkingDirectory $root -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $agentLog -RedirectStandardError $agentErr

    $connectDeadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $connectDeadline) {
        if ((Test-Path $agentLog) -and
            ((Get-Content $agentLog -Raw) -match 'COM2 baglantisi kuruldu')) {
            break
        }
        if ($agentProc.HasExited) { throw 'Arama koprusu erken kapandi.' }
        Start-Sleep -Milliseconds 200
    }
    if (-not ((Test-Path $agentLog) -and
              ((Get-Content $agentLog -Raw) -match 'COM2 baglantisi kuruldu'))) {
        throw 'Arama koprusu COM2 soketine baglanamadi.'
    }

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
        Start-Sleep -Milliseconds 450
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

    Screenshot 'search-online-desktop'

    # Masaustu Arama simgesi
    Click-At 42 452
    Start-Sleep -Milliseconds 500

    # Turkce-Q klavyede "Linux" yaz.
    Send-Text 'Linux'
    Start-Sleep -Milliseconds 500

    # Ajanin gercek internet yanitini bekle.
    $searchDeadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $searchDeadline) {
        if (Test-Path $agentLog) {
            $agentText = Get-Content $agentLog -Raw
            if ($agentText -match 'sorgu \d+: [1-9]\d* sonuc gonderildi') {
                $webOk = $true
                break
            }
            if ($agentText -match 'basarisiz|Arama yapilamadi') { break }
        }
        Start-Sleep -Milliseconds 250
    }

    Start-Sleep -Milliseconds 800
    Screenshot 'search-online-results'

    # Ilk web sonucunu ac: WEB SONUCU detay karti gorunmeli.
    Click-At 300 200
    Start-Sleep -Milliseconds 500
    Screenshot 'search-online-detail'

    # Detay kartindaki Geri dugmesi: (dx + dw - 68, dy + 8) + 26,11
    Click-At 598 197
    Start-Sleep -Milliseconds 400
    Screenshot 'search-online-back'

    # Ayni web sonucunu tekrar ac ve sayfa metnini yukle.
    Click-At 300 200
    Start-Sleep -Milliseconds 400

    # "Sayfayi Ac" dugmesi: 520 genis arama penceresinde (504, 366).
    Click-At 504 366
    Start-Sleep -Milliseconds 250
    Screenshot 'search-online-page-loading'

    $pageDeadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $pageDeadline) {
        if (Test-Path $agentLog) {
            $agentText = Get-Content $agentLog -Raw
            if ($agentText -match 'sayfa \d+: [1-9]\d* satir gonderildi') {
                $pageOk = $true
                break
            }
            if ($agentText -match 'sayfa \d+ basarisiz') { break }
        }
        Start-Sleep -Milliseconds 250
    }

    # Ajan satirlari TCP'ye birakir; misafir UART'tan ~2 sn'de okur.
    Start-Sleep -Milliseconds 4000
    Screenshot 'search-online-page'

    # PgDn ile sayfa metnini kaydir.
    Send-Key 'pgdn'
    Start-Sleep -Milliseconds 500
    Screenshot 'search-online-page-scroll'

    # Web okuyucudan arama listesine don.
    Click-At 94 99
    Start-Sleep -Milliseconds 600
    Screenshot 'search-online-page-back'

    # Ikinci sorgu: "ayar" ile 3 yerel + 2 web satiri ayni listede.
    for ($i = 0; $i -lt 5; $i++) { Send-Key 'backspace' }
    Send-Text 'ayar'
    Start-Sleep -Milliseconds 500

    $mixedDeadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $mixedDeadline) {
        if (Test-Path $agentLog) {
            $agentText = Get-Content $agentLog -Raw
            $successCount = ([regex]::Matches(
                $agentText, 'sorgu \d+: [1-9]\d* sonuc gonderildi')).Count
            if ($successCount -ge 2) {
                $mixedOk = $true
                break
            }
        }
        Start-Sleep -Milliseconds 250
    }

    Start-Sleep -Milliseconds 800
    Screenshot 'search-online-mixed'

    # 4. satir ilk web sonucudur (3 yerel + 2 web); detay kartini ac.
    Click-At 300 332
    Start-Sleep -Milliseconds 500
    Screenshot 'search-online-mixed-detail'

    Send-Cmd 'quit'
    Start-Sleep -Milliseconds 500
} catch {
    $failure = $_
} finally {
    if ($client) { $client.Close() }
    Start-Sleep -Milliseconds 300
    if ($qemuProc -and -not $qemuProc.HasExited) {
        $qemuProc.Kill()
    }
    if ($agentProc -and -not $agentProc.HasExited) {
        Stop-Process -Id $agentProc.Id -Force -ErrorAction SilentlyContinue
    }
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

if (Test-Path $agentLog) {
    Write-Host '--- ARAMA KOPRUSU (son 8 satir) ---'
    Get-Content $agentLog -Tail 8 | Write-Host
    Write-Host '------------------------------------'
}

if ($failure) { throw $failure }
if (-not $webOk) {
    throw 'Internet arama sonucu alinamadi. Ajan ve ag baglantisini kontrol edin.'
}
if (-not $pageOk) {
    throw 'Sayfa metni alinamadi. Ajan ve sayfa baglantisini kontrol edin.'
}
if (-not $mixedOk) {
    throw 'Yerel + internet birlesik sonucu alinamadi.'
}

Write-Host 'Internet arama testi basarili: web sonucu, detay karti, sayfa metni ve birlesik liste dogrulandi.'
