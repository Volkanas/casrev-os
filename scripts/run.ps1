# =============================================================================
# casrev-os - QEMU'da calistir
#   -NoDisplay : pencere acmadan calistir (seri cikti konsola gelir)
#   -Debug     : CPU hatasinda dur ve log tut
# =============================================================================
param(
    [switch]$NoDisplay,
    [switch]$Debug
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$img  = Join-Path $root 'build\casrev-os.img'

if (-not (Test-Path $img)) {
    Write-Host 'Imaj yok, once derleniyor...'
    & (Join-Path $PSScriptRoot 'build.ps1')
}

$qemu = 'C:\Program Files\qemu\qemu-system-i386.exe'
if (-not (Test-Path $qemu)) {
    $cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source } else { throw 'QEMU bulunamadi. winget install SoftwareFreedomConservancy.QEMU' }
}

$qemuArgs = @(
    '-drive', "format=raw,if=floppy,file=$img",
    '-boot', 'a',
    '-m', '32M',
    '-serial', 'stdio',
    '-vga', 'std'
)

if ($NoDisplay) { $qemuArgs += @('-display', 'none') }
if ($Debug)     { $qemuArgs += @('-no-reboot', '-no-shutdown', '-d', 'int,cpu_reset') }

Write-Host "QEMU baslatiliyor: $qemu"
& $qemu @qemuArgs
