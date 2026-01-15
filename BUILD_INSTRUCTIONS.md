# Building Squeezelite ESP32-S3 from Source

## Requirements

1. **ESP-IDF v5.3.1** or later
   - Download: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/
   - Windows: Use ESP-IDF Tools Installer
   - Linux/macOS: Follow official guide

2. **Python 3.8+**

3. **Git**

## Build Steps

### Linux / macOS

`ash
# Clone repository
git clone https://github.com/YOUR_USER/squeezelite-esp32s3.git
cd squeezelite-esp32s3/source

# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash (connect ESP32-S3 via USB first)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
`

### Windows (Command Prompt)

`cmd
:: Clone repository
git clone https://github.com/YOUR_USER/squeezelite-esp32s3.git
cd squeezelite-esp32s3\source

:: Open ESP-IDF Command Prompt (from Start Menu)
:: Or set up environment manually:
C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat

:: Build
idf.py build

:: Flash
idf.py -p COM24 flash

:: Monitor
idf.py -p COM24 monitor
`

### Windows (PowerShell with provided scripts)

`powershell
cd squeezelite-esp32s3\source

# Build
.\build_with_env.ps1

# Flash  
.\flash_with_env.ps1

# Monitor
.\monitor_with_env.ps1
`

## Output Files

After successful build, files are in uild/ folder:
- squeezelite.bin - Main application
- ootloader/bootloader.bin - Bootloader
- partition_table/partition-table.bin - Partition table
- ota_data_initial.bin - OTA boot data

## Flash Addresses

| File | Address |
|------|---------|
| bootloader.bin | 0x0 |
| partition-table.bin | 0x8000 |
| ota_data_initial.bin | 0x49000 |
| squeezelite.bin | 0x1D0000 |

## Manual Flash (without idf.py)

`ash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x49000 build/ota_data_initial.bin \
  0x1D0000 build/squeezelite.bin
`

Or flash combined image:
`ash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 write_flash 0x0 firmware/squeezelite_full.bin
`

## Troubleshooting

### Build fails with missing components
Run: idf.py reconfigure

### Permission denied on Linux
Add user to dialout group: sudo usermod -a -G dialout $USER
Then log out and back in.

### Port not found on Windows
Install CP210x or CH340 USB drivers.

## Configuration

Edit sdkconfig or run idf.py menuconfig to change:
- WiFi settings
- Audio output (I2S/SPDIF)
- GPIO pins
- Enable/disable services (Spotify, AirPlay, Bluetooth)
