# Monitor ESP32-S3 Audio Project with Environment Setup
param(
    [string]$Port = "COM24",
    [switch]$NoReset
)

Write-Host "================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 Audio - Monitor" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

$comPort = $Port
$pythonPath = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe"

Write-Host "Using COM port: $comPort" -ForegroundColor Green
Write-Host ""

# Reset device before starting monitor (unless -NoReset specified)
if (-not $NoReset) {
    Write-Host "Resetting device..." -ForegroundColor Yellow
    & $pythonPath -m esptool --chip esp32s3 -p $comPort run 2>$null
    Start-Sleep -Milliseconds 500
    Write-Host "Device reset complete" -ForegroundColor Green
    Write-Host ""
}

Write-Host "Starting monitor on $comPort..." -ForegroundColor Yellow
Write-Host "Press Ctrl+C to exit" -ForegroundColor Gray
Write-Host ""

# Use miniterm with auto-reset via DTR/RTS
& $pythonPath -m serial.tools.miniterm --raw --rts 0 --dtr 0 $comPort 115200
