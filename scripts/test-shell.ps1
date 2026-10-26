# =============================================================================
# casrev-os - klavye + kabuk uctan uca testi
#   QEMU monitor uzerinden sendkey ile tus basislari gonderir, seri ciktiyi
#   ve ekran goruntusunu dogrular.
# =============================================================================
param(
    [int]$BootWaitSec = 3,
    [int]$MonitorPort = 5561
)

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$img   = Join-Path $root 'build\casrev-os.img'
$log   = Join-Path $root 'build\serial-shell.log'
$ppm   = Join-Path $root 'build\shell.ppm'
$png   = Join-Path $root 'build\shell.png'
$qemu  = 'C:\Program Files\qemu\qemu-system-i386.exe'

if (-not (Test-Path $img)) { & (Join-Path $PSScriptRoot 'build.ps1') }
if (-not (Test-Path $qemu)) {
    $cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source } else { throw 'QEMU bulunamadi.' }
}

Remove-Item $log, $ppm, $png -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $qemu `
    -ArgumentList '-drive', 'format=raw,if=floppy,file=build/casrev-os.img',
                  '-boot', 'a', '-m', '32M', '-display', 'none',
                  '-serial', 'file:build/serial-shell.log',
                  '-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait" `
    -WorkingDirectory $root -PassThru -WindowStyle Hidden

try {
    Start-Sleep -Seconds $BootWaitSec

    $client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', $MonitorPort)
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $writer.AutoFlush = $true
    Start-Sleep -Milliseconds 300

    function Send-Text([string]$text) {
        foreach ($ch in $text.ToCharArray()) {
            if ($ch -eq ' ') {
                $key = 'spc'
            } elseif ($ch -eq "`n") {
                $key = 'ret'
            } elseif ($ch -eq "`b") {
                $key = 'backspace'
            } elseif ($ch -eq 'i') {
                # Turkce Q duzeninde 'i' harfi fiziksel apostrophe tusundadir.
                $key = 'apostrophe'
            } elseif ($ch -eq '.') {
                # Turkce Q duzeninde '.' fiziksel slash tusundadir.
                $key = 'slash'
            } elseif ($ch -eq ',') {
                $key = 'backslash'
            } elseif ($ch -eq '-') {
                $key = 'minus'
            } elseif ($ch -eq '=') {
                $key = 'equal'
            } elseif ($ch -eq '\') {
                $key = 'grave_accent'
            } else {
                $key = [string]$ch
            }
            $writer.WriteLine("sendkey $key")
            Start-Sleep -Milliseconds 120
        }
    }

    function Send-Command([string]$command) {
        Send-Text ($command + "`n")
        Start-Sleep -Milliseconds 800
    }

    Send-Command 'help'
    Send-Command 'uptime'
    Send-Command 'mem'
    Send-Command 'frame'
    Send-Command 'heap'
    Send-Command 'echo merhaba dunya'
    Send-Command 'time'

    # Fare testi: isinma hareketi, taban cizgi, gercek hareket, dugmeler.
    Send-Command 'mouse'
    $writer.WriteLine('mouse_move 1 1')
    Start-Sleep -Milliseconds 500
    Send-Command 'mouse'
    $writer.WriteLine('mouse_move 24 16')
    Start-Sleep -Milliseconds 500
    Send-Command 'mouse'
    $writer.WriteLine('mouse_button 1')
    Start-Sleep -Milliseconds 250
    Send-Command 'mouse'
    $writer.WriteLine('mouse_button 0')
    Start-Sleep -Milliseconds 250
    Send-Command 'mouse'

    $writer.WriteLine('screendump build/shell.ppm')
    Start-Sleep -Seconds 1
    $writer.WriteLine('quit')
    Start-Sleep -Milliseconds 500

    $client.Close()
} finally {
    Start-Sleep -Milliseconds 300
    if (-not $p.HasExited) { $p.Kill() }
}

if (-not (Test-Path $log)) { throw 'Seri cikti alinamadi.' }
$out = Get-Content $log -Raw

Write-Host '--- SERI CIKTI ---'
Write-Host $out
Write-Host '------------------'

$checks = @(
    @{ Name = 'Kabuk istemi';      Needle = 'casrev> ' },
    @{ Name = 'help komutu';       Needle = 'Komutlar:' },
    @{ Name = 'uptime komutu';     Needle = 'Calisma suresi:' },
    @{ Name = 'mem komutu';        Needle = 'Sayfalama: ACIK' },
    @{ Name = 'frame komutu';      Needle = 'Kare ayrildi:' },
    @{ Name = 'heap komutu';       Needle = 'kmalloc:' },
    @{ Name = 'echo komutu';       Needle = 'merhaba dunya' },
    @{ Name = 'time komutu';       Needle = 'Saat: ' },
    @{ Name = 'RTC tarihi';        Needle = 'Tarih: ' },
    @{ Name = 'mouse komutu';      Needle = 'Fare: x=' },
    @{ Name = 'Temiz istem';       Needle = "casrev> echo merhaba dunya" }
)

$failed = @()
foreach ($c in $checks) {
    if ($out -like "*$($c.Needle)*") {
        Write-Host ("[OK]   {0}" -f $c.Name)
    } else {
        Write-Host ("[FAIL] {0}" -f $c.Name)
        $failed += $c.Name
    }
}

# Fare hareketi ve dugme testi: 5 adet "Fare: x=.. y=.. dugmeler=.." satiri
# bekleniyor (ilk durum, taban, hareket, basili, birakildi).
$mouseStates = [regex]::Matches(
    $out, 'Fare: x=(-?\d+) y=(-?\d+) dugmeler=(\d+)')

$moveOk = $false
$buttonOk = $false
if ($mouseStates.Count -ge 5) {
    $baseX = [int]$mouseStates[1].Groups[1].Value
    $baseY = [int]$mouseStates[1].Groups[2].Value
    $newX  = [int]$mouseStates[2].Groups[1].Value
    $newY  = [int]$mouseStates[2].Groups[2].Value
    $moveOk = (($newX - $baseX) -eq 24) -and (($newY - $baseY) -eq 16)

    $press   = $mouseStates[3].Groups[3].Value
    $release = $mouseStates[4].Groups[3].Value
    $buttonOk = ($press -eq '1') -and ($release -eq '0')
}

if ($moveOk) {
    Write-Host '[OK]   Fare hareketi (IRQ12)'
} else {
    Write-Host '[FAIL] Fare hareketi (IRQ12)'
    $failed += 'Fare hareketi (IRQ12)'
}
if ($buttonOk) {
    Write-Host '[OK]   Fare dugmeleri'
} else {
    Write-Host '[FAIL] Fare dugmeleri'
    $failed += 'Fare dugmeleri'
}

if (Test-Path $ppm) {
    python (Join-Path $root 'build\ppm2png.py') $ppm $png
    Write-Host "Ekran goruntusu: $png"
}

if ($failed.Count -gt 0) {
    Write-Error ("Basarisiz testler: {0}" -f ($failed -join ', '))
    exit 1
}
Write-Host 'Klavye + kabuk testi gecti.'
