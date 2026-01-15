# Build ESP32S3 Audio Native - STANDALONE BUILD SCRIPT
# No dependencies on other projects

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "ESP32S3 Audio Native - Build" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

Write-Host "Project directory: $scriptDir" -ForegroundColor Yellow
Write-Host ""

# Setup ESP-IDF environment
Write-Host "Setting up ESP-IDF environment..." -ForegroundColor Yellow
& 'C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1' | Out-Null

Write-Host ""
Write-Host "Configuring project..." -ForegroundColor Yellow
$null = & 'C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe' 'C:\Espressif\frameworks\esp-idf-v5.3.1\tools\idf.py' set-target esp32s3 2>&1

Write-Host ""
Write-Host "Building firmware..." -ForegroundColor Yellow
$buildOutput = & 'C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe' 'C:\Espressif\frameworks\esp-idf-v5.3.1\tools\idf.py' build 2>&1

# Display output
$buildOutput | ForEach-Object { Write-Host $_ }

Write-Host ""
# Check result
if ($buildOutput -match "error|failed") {
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    exit 1
} elseif ($buildOutput -match "Leaving|successful") {
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Build completed." -ForegroundColor Green
    exit 0
}
