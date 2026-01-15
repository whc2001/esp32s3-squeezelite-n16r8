# Flash ESP32-S3 Audio Project with Environment Setup (ESP-IDF 5.3)
# IMPORTANT: Always flashes to ota_0 partition (0x1D0000) for Squeezelite main app
# Recovery partition (0x50000) is intentionally smaller and not used for main app
param(
    [string]$Port = "COM24",
    [int]$Baud = 460800,
    [switch]$FullFlash,  # Use this to flash bootloader + partition table + ota_data + app
    [switch]$EraseAll    # Use this to erase all flash before flashing
)

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 Audio - Flash to OTA_0 (IDF 5.3)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "Port: $Port, Baud: $Baud" -ForegroundColor Yellow
Write-Host "Target partition: ota_0 @ 0x1D0000" -ForegroundColor Yellow
Write-Host ""

# Always run from project directory (script folder)
Set-Location -Path $PSScriptRoot

# Setup ESP-IDF 5.3.1 environment
Write-Host "Setting up ESP-IDF 5.3.1 environment..." -ForegroundColor Yellow
& 'C:\Espressif\Espressif\frameworks\esp-idf-v5.3.1\export.ps1'

# Verify build files exist
$buildDir = Join-Path $PSScriptRoot "build"
$bootloader = Join-Path $buildDir "bootloader\bootloader.bin"
$partTable = Join-Path $buildDir "partition_table\partition-table.bin"
$otaData = Join-Path $buildDir "ota_data_initial.bin"
$appBin = Join-Path $buildDir "squeezelite.bin"

if (-not (Test-Path $appBin)) {
    Write-Host "ERROR: squeezelite.bin not found! Run build first." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Flashing to OTA_0 partition..." -ForegroundColor Yellow

if ($EraseAll) {
    Write-Host "Erasing all flash first..." -ForegroundColor Yellow
    esptool.py -p $Port erase_flash
}

if ($FullFlash -or $EraseAll -or -not (Test-Path (Join-Path $buildDir ".flashed_once"))) {
    # Full flash: bootloader + partition table + ota_data + app to ota_0
    Write-Host "Full flash: bootloader + partition-table + ota_data + squeezelite.bin -> ota_0" -ForegroundColor Cyan
    esptool.py -p $Port -b $Baud --chip esp32s3 write_flash `
        --flash_mode dio --flash_freq 80m `
        0x0 $bootloader `
        0x8000 $partTable `
        0x49000 $otaData `
        0x1D0000 $appBin
    $code = $LASTEXITCODE
    
    if ($code -eq 0) {
        # Mark as flashed once
        "Flashed: $(Get-Date)" | Out-File (Join-Path $buildDir ".flashed_once")
    }
} else {
    # App-only flash to ota_0
    Write-Host "App-only flash: squeezelite.bin -> ota_0 @ 0x1D0000" -ForegroundColor Cyan
    esptool.py -p $Port -b $Baud --chip esp32s3 write_flash `
        --flash_mode dio --flash_freq 80m `
        0x1D0000 $appBin
    $code = $LASTEXITCODE
}

if ($code -ne 0) {
    Write-Host "Flash FAILED! (exit code: $code)" -ForegroundColor Red
    exit $code
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Flash SUCCESSFUL to OTA_0 partition!" -ForegroundColor Green
Write-Host "Device will boot Squeezelite from ota_0" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
exit 0
