@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo   casrev-os baslatiliyor
echo ==========================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1"
if errorlevel 1 (
    echo.
    echo Derleme basarisiz oldu.
    pause
    exit /b 1
)

echo.
echo QEMU penceresi aciliyor...
echo Kapatmak icin QEMU penceresini kapatin.
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\run.ps1"

if errorlevel 1 pause
endlocal
