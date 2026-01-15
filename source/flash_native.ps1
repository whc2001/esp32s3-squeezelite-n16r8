# Flash ESP32S3 Audio Native - STANDALONE FLASH SCRIPT
# IMPORTANT: Always flashes to ota_0 partition (0x1D0000) - firmware is too big for recovery
# No dependencies on other projects

param(
    [string]$Port = "COM24",
    [int]$Baud = 460800,
    [switch]$FullFlash,  # Flash bootloader + partition table + ota_data + app
    [switch]$EraseAll    # Erase all flash first
)

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "ESP32S3 Audio Native - Flash to OTA_0" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "Target: ota_0 @ 0x1D0000" -ForegroundColor Yellow
Write-Host ""

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

Write-Host "Project directory: $scriptDir" -ForegroundColor Yellow

# Build paths
$buildDir = Join-Path $scriptDir "build"
$bootloader = Join-Path $buildDir "bootloader\bootloader.bin"
$partTable = Join-Path $buildDir "partition_table\partition-table.bin"
$otaData = Join-Path $buildDir "ota_data_initial.bin"
$appBin = Join-Path $buildDir "squeezelite.bin"

# Check if app exists
if (-not (Test-Path $appBin)) {
    Write-Host "ERROR: squeezelite.bin not found! Run build first." -ForegroundColor Red
    exit 1
}

# Auto-detect COM port if not provided
if ($Port -eq "COM24") {
    $detected = Get-WmiObject Win32_SerialPort | Select-Object -ExpandProperty DeviceID -First 1
    if ($detected) { $Port = $detected }
}

Write-Host "Port: $Port, Baud: $Baud" -ForegroundColor Yellow
Write-Host ""

# Python with esptool
$python = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe"

if ($EraseAll) {
    Write-Host "Erasing all flash..." -ForegroundColor Yellow
    & $python -m esptool --chip esp32s3 -p $Port erase_flash
}

if ($FullFlash -or $EraseAll) {
    # Full flash: bootloader + partition table + ota_data + app to ota_0
    Write-Host "Full flash: bootloader + partition-table + ota_data + app -> ota_0" -ForegroundColor Cyan
    & $python -m esptool --chip esp32s3 -p $Port -b $Baud `
        --before default_reset --after hard_reset write_flash `
        --flash_mode dio --flash_size 16MB --flash_freq 80m `
        0x0 $bootloader `
        0x8000 $partTable `
        0x49000 $otaData `
        0x1D0000 $appBin
} else {
    # App-only flash to ota_0
    Write-Host "App-only flash: squeezelite.bin -> ota_0 @ 0x1D0000" -ForegroundColor Cyan
    & $python -m esptool --chip esp32s3 -p $Port -b $Baud `
        --before default_reset --after hard_reset write_flash `
        --flash_mode dio --flash_size 16MB --flash_freq 80m `
        0x1D0000 $appBin
}

$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host "FLASH FAILED! (exit code: $code)" -ForegroundColor Red
    exit $code
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Flash SUCCESSFUL to OTA_0 partition!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
exit 0
