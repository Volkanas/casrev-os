# =============================================================================
# casrev-os - otomatik duman testi
#   Imaji QEMU'da penceresiz calistirir, seri ciktiyi dosyaya alir ve
#   beklenen kilometre taslarinin olusup olusmadigini kontrol eder.
# =============================================================================
param(
    [int]$TimeoutSec = 5
)

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$img   = Join-Path $root 'build\casrev-os.img'
$log   = Join-Path $root 'build\serial.log'
$qemu  = 'C:\Program Files\qemu\qemu-system-i386.exe'

if (-not (Test-Path $img)) { & (Join-Path $PSScriptRoot 'build.ps1') }
if (-not (Test-Path $qemu)) {
    $cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source } else { throw 'QEMU bulunamadi.' }
}

Remove-Item $log -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $qemu `
    -ArgumentList '-drive', 'format=raw,if=floppy,file=build/casrev-os.img',
                  '-boot', 'a', '-m', '32M', '-display', 'none',
                  '-serial', 'file:build/serial.log', '-monitor', 'none' `
    -WorkingDirectory $root -PassThru -WindowStyle Hidden

Start-Sleep -Seconds $TimeoutSec
if (-not $p.HasExited) { $p.Kill() }
Start-Sleep -Milliseconds 300

if (-not (Test-Path $log)) { throw 'Seri cikti alinamadi.' }
$out = Get-Content $log -Raw

Write-Host '--- SERI CIKTI ---'
Write-Host $out
Write-Host '------------------'

$checks = @(
    @{ Name = 'Boot sector';        Needle = 'Onyukleme sektoru calisiyor.' },
    @{ Name = 'stage2 yuklendi';    Needle = 'stage2 diskten yuklendi.' },
    @{ Name = 'E820 haritasi';      Needle = 'E820 bellek haritasi alindi.' },
    @{ Name = 'Kernel diskten';     Needle = 'Cekirdek diskten 0x10000 adresine yuklendi.' },
    @{ Name = 'A20 acildi';         Needle = 'A20 hatti acildi.' },
    @{ Name = 'Cekirdek v0.9';      Needle = 'casrev-os v0.9' },
    @{ Name = 'PMM hazir';          Needle = 'PMM: ' },
    @{ Name = 'Sayfalama acik';     Needle = 'Sayfalama : ACIK' },
    @{ Name = 'Heap testi';         Needle = 'Heap      : 256 KB, test OK' },
    @{ Name = 'IDT kuruldu';        Needle = 'IDT    : 256 giris' },
    @{ Name = 'PIT 100 Hz';         Needle = 'PIT    : 100 Hz' },
    @{ Name = 'Kabuk istemi';       Needle = 'casrev> ' }
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

if ($failed.Count -gt 0) {
    Write-Error ("Basarisiz testler: {0}" -f ($failed -join ', '))
    exit 1
}
Write-Host 'Tum testler gecti.'
