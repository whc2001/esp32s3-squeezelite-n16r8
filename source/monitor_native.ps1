# Monitor ESP32S3 Audio Native - STANDALONE MONITOR SCRIPT
# No dependencies on other projects

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "ESP32S3 Audio Native - Monitor" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

Write-Host "Project directory: $scriptDir" -ForegroundColor Yellow
Write-Host ""

# Auto-detect COM port
Write-Host "Detecting COM port..." -ForegroundColor Yellow
$ports = Get-WmiObject Win32_SerialPort | Select-Object -ExpandProperty Name
$comPort = $ports[0]

if ($null -eq $comPort) {
    Write-Host "ERROR: No COM port detected!" -ForegroundColor Red
    Write-Host "Please connect ESP32 device and try again." -ForegroundColor Red
    exit 1
}

Write-Host "Found COM port: $comPort" -ForegroundColor Green
Write-Host ""

# Setup ESP-IDF environment
Write-Host "Setting up ESP-IDF environment..." -ForegroundColor Yellow
& 'C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1' | Out-Null

Write-Host ""
Write-Host "Starting monitor on $comPort..." -ForegroundColor Yellow
Write-Host "Press Ctrl+] to exit" -ForegroundColor Gray
Write-Host ""

& 'C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe' 'C:\Espressif\frameworks\esp-idf-v5.3.1\tools\idf.py' -p $comPort monitor
