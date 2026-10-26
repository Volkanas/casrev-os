param(
    [int]$BootWaitSec = 3,
    [int]$MonitorPort = 5565
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$img  = Join-Path $root 'build\casrev-os.img'
$log  = Join-Path $root 'build\serial-gui.log'
$ppm  = Join-Path $root 'build\gui.ppm'
$png  = Join-Path $root 'build\gui.png'
$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'

Remove-Item $log, $ppm, $png -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $qemu `
    -ArgumentList '-drive', 'format=raw,if=floppy,file=build/casrev-os.img',
                  '-boot', 'a', '-m', '32M', '-display', 'none',
                  '-serial', 'file:build/serial-gui.log',
                  '-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait" `
    -WorkingDirectory $root -PassThru -WindowStyle Hidden

try {
    Start-Sleep -Seconds $BootWaitSec
    $client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', $MonitorPort)
    $writer = New-Object System.IO.StreamWriter($client.GetStream())
    $writer.AutoFlush = $true

    function Send-Cmd([string]$cmd) {
        foreach ($ch in $cmd.ToCharArray()) {
            $key = if ($ch -eq ' ') { 'spc' }
                   elseif ($ch -eq "`n") { 'ret' }
                   elseif ($ch -eq 'i') { 'apostrophe' }
                   elseif ($ch -eq '.') { 'slash' }
                   elseif ($ch -eq ',') { 'backslash' }
                   elseif ($ch -eq '-') { 'minus' }
                   elseif ($ch -eq '=') { 'equal' }
                   elseif ($ch -eq '\') { 'grave_accent' }
                   else { [string]$ch }
            $writer.WriteLine("sendkey $key")
            Start-Sleep -Milliseconds 60
        }
        $writer.WriteLine("sendkey ret")
        Start-Sleep -Milliseconds 400
    }

    Send-Cmd 'notepad readme.txt'
    Start-Sleep -Milliseconds 500
    $writer.WriteLine('screendump build/gui.ppm')
    Start-Sleep -Seconds 1
    $writer.WriteLine('quit')
    $client.Close()
} finally {
    Start-Sleep -Milliseconds 200
    if (-not $p.HasExited) { $p.Kill() }
}

if (Test-Path $ppm) {
    python (Join-Path $root 'build\ppm2png.py') $ppm $png
    Write-Host "Ekran goruntusu hazir: $png"
}
