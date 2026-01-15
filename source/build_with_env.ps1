# Build ESP32-S3 Audio Project with Environment Setup
Write-Host "================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 Audio - Build" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

# Setup ESP-IDF environment
Write-Host "Setting up ESP-IDF environment..." -ForegroundColor Yellow
& 'C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1'

Write-Host ""
Write-Host "Building firmware..." -ForegroundColor Yellow
idf.py build 2>&1 | Tee-Object -Variable buildOutput

# Check for errors
if ($buildOutput -match "error") {
    Write-Host "Build FAILED!" -ForegroundColor Red
    exit 1
} elseif ($buildOutput -match "Leaving") {
    Write-Host ""
    Write-Host "Build SUCCESSFUL!" -ForegroundColor Green
    Write-Host ""
    exit 0
} else {
    Write-Host ""
    Write-Host "Build completed." -ForegroundColor Green
    exit 0
}
