# =============================================================================
# casrev-os derleme betigi
#
#   1. C cekirdegi  : src/kernel/*.c + entry.asm  -> build/kernel.bin (0x100000)
#   2. stage2       : src/boot/stage2.asm         -> build/stage2.bin (0x7E00)
#   3. boot sector  : src/boot/boot.asm           -> build/boot.bin   (0x7C00)
#   4. disket imaji : build/casrev-os.img         -> 1.44 MB
#
# Imaj yerlesimi:
#   sektor 0     : boot sector      (512 bayt)
#   sektor 1-4   : stage2           (4 sektor = 2048 bayt)
#   sektor 5+    : kernel.bin       (KERNEL_SECTORS sektor)
# =============================================================================
$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$build     = Join-Path $root 'build'
$objDir    = Join-Path $build 'obj'
$kernelDir = Join-Path $root 'src\kernel'
$bootDir   = Join-Path $root 'src\boot'

New-Item -ItemType Directory -Force -Path $build, $objDir | Out-Null

$STAGE2_SECTORS = 4
$STAGE2_SIZE    = $STAGE2_SECTORS * 512
$KERNEL_OFFSET  = 512 + $STAGE2_SIZE

# --- Arac yollari -----------------------------------------------------------
function Find-Tool([string]$name, [string]$preferred) {
    if (Test-Path $preferred) { return $preferred }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "$name bulunamadi."
}

$nasm    = Find-Tool 'nasm'    'C:\Users\smmav\AppData\Local\bin\NASM\nasm.exe'
$clang   = Find-Tool 'clang'   'C:\Program Files\LLVM\bin\clang.exe'
$lld     = Find-Tool 'ld.lld'  'C:\Program Files\LLVM\bin\ld.lld.exe'
$objcopy = Find-Tool 'llvm-objcopy' 'C:\Program Files\LLVM\bin\llvm-objcopy.exe'

# --- 1) Cekirdek ------------------------------------------------------------
Write-Host '[1/4] Cekirdek derleniyor...'

$cSources = @('kernel.c', 'vga.c', 'ui.c', 'gfx.c', 'vfs.c', 'serial.c', 'console.c',
              'string.c', 'idt.c', 'timer.c', 'keyboard.c', 'mouse.c',
              'rtc.c', 'shell.c', 'pmm.c', 'paging.c', 'heap.c')
$cflags = @(
    '--target=i386-elf',
    '-march=i386',
    '-mno-sse', '-mno-mmx', '-msoft-float',
    '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-pic',
    '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
    '-fcf-protection=none',
    '-O2', '-Wall', '-Wextra', '-std=c11'
)

$cObjects = @()
foreach ($src in $cSources) {
    $obj = Join-Path $objDir ([IO.Path]::GetFileNameWithoutExtension($src) + '.o')
    & $clang @cflags -c (Join-Path $kernelDir $src) -o $obj
    if ($LASTEXITCODE -ne 0) { throw "C derlemesi basarisiz: $src" }
    $cObjects += $obj
}

$entryObj = Join-Path $build 'entry.o'
& $nasm -f elf32 -o $entryObj (Join-Path $kernelDir 'entry.asm')
if ($LASTEXITCODE -ne 0) { throw 'entry.asm derlemesi basarisiz.' }

$interruptsObj = Join-Path $build 'interrupts.o'
& $nasm -f elf32 -o $interruptsObj (Join-Path $kernelDir 'interrupts.asm')
if ($LASTEXITCODE -ne 0) { throw 'interrupts.asm derlemesi basarisiz.' }

$elf       = Join-Path $build 'kernel.elf'
$kernelBin = Join-Path $build 'kernel.bin'
$linker    = Join-Path $kernelDir 'linker.ld'

& $lld -m elf_i386 -T $linker -o $elf $entryObj $interruptsObj @cObjects
if ($LASTEXITCODE -ne 0) { throw 'Cekirdek baglama (link) basarisiz.' }

& $objcopy -O binary $elf $kernelBin
if ($LASTEXITCODE -ne 0) { throw 'kernel.elf -> kernel.bin donusumu basarisiz.' }

$kernelBytes   = [IO.File]::ReadAllBytes($kernelBin)
$kernelSectors = [int][Math]::Ceiling($kernelBytes.Length / 512.0)
if ($kernelSectors -lt 1) { throw 'Cekirdek bos.' }
if ($kernelSectors -gt 256) { throw "Cekirdek cok buyuk: $kernelSectors sektor (en fazla 256)." }

$kernelPadded = New-Object byte[] ($kernelSectors * 512)
[Array]::Copy($kernelBytes, 0, $kernelPadded, 0, $kernelBytes.Length)

# --- 2) stage2 --------------------------------------------------------------
Write-Host "[2/4] stage2 derleniyor (kernel = $kernelSectors sektor, baslangic sektoru 6)..."
$stage2Bin = Join-Path $build 'stage2.bin'
& $nasm -f bin "-DKERNEL_SECTORS=$kernelSectors" -o $stage2Bin (Join-Path $bootDir 'stage2.asm')
if ($LASTEXITCODE -ne 0) { throw 'stage2 derlemesi basarisiz.' }

$stage2Bytes = [IO.File]::ReadAllBytes($stage2Bin)
if ($stage2Bytes.Length -gt $STAGE2_SIZE) {
    throw "stage2 $STAGE2_SIZE bayti asiyor: $($stage2Bytes.Length) bayt."
}
$stage2Padded = New-Object byte[] $STAGE2_SIZE
[Array]::Copy($stage2Bytes, 0, $stage2Padded, 0, $stage2Bytes.Length)

# --- 3) boot sector ---------------------------------------------------------
Write-Host '[3/4] boot sector derleniyor...'
$bootBin = Join-Path $build 'boot.bin'
& $nasm -f bin "-DSTAGE2_SECTORS=$STAGE2_SECTORS" -o $bootBin (Join-Path $bootDir 'boot.asm')
if ($LASTEXITCODE -ne 0) { throw 'boot sector derlemesi basarisiz.' }

$bootBytes = [IO.File]::ReadAllBytes($bootBin)
if ($bootBytes.Length -ne 512) {
    throw "Onyukleme sektoru 512 bayt olmali, $($bootBytes.Length) bayt uretildi."
}
if ($bootBytes[510] -ne 0x55 -or $bootBytes[511] -ne 0xAA) {
    throw 'Onyukleme imzasi (0xAA55) eksik.'
}

# --- 4) imaj ----------------------------------------------------------------
Write-Host '[4/4] 1.44 MB imaj olusturuluyor...'
$imgPath  = Join-Path $build 'casrev-os.img'
$imgBytes = New-Object byte[] 1474560

[Array]::Copy($bootBytes,     0,            $imgBytes, 0,            512)
[Array]::Copy($stage2Padded,  0,            $imgBytes, 512,          $STAGE2_SIZE)
[Array]::Copy($kernelPadded,  0,            $imgBytes, $KERNEL_OFFSET, $kernelPadded.Length)
[IO.File]::WriteAllBytes($imgPath, $imgBytes)

Write-Host ''
Write-Host 'Tamam:'
Write-Host ("  boot.bin   : {0,5} bayt  (sektor 0)" -f $bootBytes.Length)
Write-Host ("  stage2.bin : {0,5} bayt  (sektor 1-{1})" -f $stage2Bytes.Length, $STAGE2_SECTORS)
Write-Host ("  kernel.bin : {0,5} bayt  (sektor 5+, {1} sektor)" -f $kernelBytes.Length, $kernelSectors)
Write-Host ("  imaj       : {0}" -f $imgPath)
Write-Host ''
Write-Host 'Calistir: powershell -ExecutionPolicy Bypass -File scripts/run.ps1'
