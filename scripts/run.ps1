# =============================================================================
# casrev-os - QEMU'da calistir
#   -NoDisplay  : pencere acmadan calistir (seri cikti konsola gelir)
#   -Debug      : CPU hatasinda dur ve log tut
#   -NoInternet : host arama koprusunu baslatma (yalnizca yerel arama)
# =============================================================================
param(
    [switch]$NoDisplay,
    [switch]$Debug,
    [switch]$NoInternet
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$img       = Join-Path $root 'build\casrev-os.img'
$agent     = Join-Path $PSScriptRoot 'search-agent.py'
$agentLog  = Join-Path $root 'build\search-agent.log'
$agentErr  = Join-Path $root 'build\search-agent.err.log'
$com2Port  = 5568
$agentProc = $null

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
    '-serial', 'stdio'
)

$internet = -not $NoInternet
if ($internet) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python -and (Get-Command py -ErrorAction SilentlyContinue)) {
        $python = Get-Command py
    }

    if ($python) {
        $pythonExe = $python.Source
        $pythonArgs = @($agent, '--host', '127.0.0.1', '--port', "$com2Port", '--wait', '30')
        if ($python.Name -ieq 'py.exe' -or $python.Name -ieq 'py') {
            $pythonArgs = @('-3') + $pythonArgs
        }

        Remove-Item $agentLog, $agentErr -ErrorAction SilentlyContinue
        Write-Host "Internet koprusu baslatiliyor (COM2 tcp:$com2Port)..."
        $agentProc = Start-Process -FilePath $pythonExe -ArgumentList $pythonArgs `
            -WorkingDirectory $root -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $agentLog -RedirectStandardError $agentErr
        $qemuArgs += @('-serial', "tcp:127.0.0.1:$com2Port,server=on,wait=off")
    } else {
        Write-Host '[UYARI] Python bulunamadi; internet aramasi kapali.'
        $internet = $false
    }
}

$qemuArgs += @('-vga', 'std')

if ($NoDisplay) { $qemuArgs += @('-display', 'none') }
if ($Debug)     { $qemuArgs += @('-no-reboot', '-no-shutdown', '-d', 'int,cpu_reset') }

try {
    Write-Host "QEMU baslatiliyor: $qemu"
    & $qemu @qemuArgs
} finally {
    if ($agentProc -and -not $agentProc.HasExited) {
        Stop-Process -Id $agentProc.Id -Force -ErrorAction SilentlyContinue
    }
    if (-not $internet -and -not $NoInternet) {
        Write-Host 'Internet aramasi olmadan devam edildi.'
    }
}
