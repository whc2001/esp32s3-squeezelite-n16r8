# 🎵 Squeezelite ESP32-S3 Audio Player
Source files i will upload Soon
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.1-blue)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-green)](https://www.espressif.com/en/products/socs/esp32-s3)
[![License](https://img.shields.io/badge/License-GPL--3.0-orange)](LICENSE)

**Squeezelite** is a high-quality audio player for **ESP32-S3** with support for:
- 🎧 **Logitech Media Server (LMS)** streaming
- 🎵 **Spotify Connect** (cspot)
- 🍎 **AirPlay** receiver
- 📻 **Bluetooth A2DP** sink
- 🔊 **SPDIF / I2S / DAC** output

---

## 🚀 Quick Start - Flash Prebuilt Firmware

### Option 1: Web Flasher (Easiest)
1. Go to [web.esphome.io](https://web.esphome.io)
2. Connect your ESP32-S3 via USB
3. Select `esphome_flash/squeezelite_full.bin`
4. Click **INSTALL**

### Option 2: ESP Web Tools
Open `web_flash/index.html` in Chrome/Edge and follow instructions.

### Option 3: Command Line
```bash
esptool.py --chip esp32s3 -p COM24 -b 460800 write_flash 0x0 firmware/squeezelite_full.bin
```

---

## 📦 Prebuilt Firmware

| File | Description | Size |
|------|-------------|------|
| `firmware/squeezelite_full.bin` | Complete image (flash at 0x0) | ~4.2 MB |
| `firmware/squeezelite.bin` | App only (flash at 0x1D0000) | ~2.3 MB |
| `firmware/bootloader.bin` | Bootloader (0x0) | ~21 KB |
| `firmware/partition-table.bin` | Partition table (0x8000) | ~3 KB |
| `firmware/ota_data_initial.bin` | OTA data (0x49000) | ~8 KB |

---

## 🔧 Building from Source

### Requirements
- **ESP-IDF v5.3.1** or later
- **Python 3.8+**
- **Git**

### Build Steps

```bash
# Clone repository
git clone https://github.com/YOUR_USER/squeezelite-esp32s3.git
cd squeezelite-esp32s3/source

# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash (full)
idf.py -p /dev/ttyUSB0 flash

# Or flash app only to OTA_0
esptool.py --chip esp32s3 -p /dev/ttyUSB0 write_flash 0x1D0000 build/squeezelite.bin
```

### Windows (PowerShell)
```powershell
# Use provided scripts
.\build_with_env.ps1
.\flash_with_env.ps1
```

---

## ⚡ Flash Addresses (Partition Layout)

| Partition | Address | Size | Description |
|-----------|---------|------|-------------|
| bootloader | 0x0 | 32 KB | ESP-IDF bootloader |
| partition-table | 0x8000 | 4 KB | Partition table |
| nvs | 0x9000 | 256 KB | NVS configuration |
| otadata | 0x49000 | 8 KB | OTA boot selection |
| phy_init | 0x4B000 | 4 KB | PHY calibration |
| recovery | 0x50000 | 1.5 MB | Factory app (unused) |
| **ota_0** | **0x1D0000** | 12.3 MB | **Main application** |
| settings | 0xE00000 | 2 MB | Additional settings |

⚠️ **Important:** Flash firmware to **OTA_0 (0x1D0000)**, not recovery partition!

---

## 🌐 First Time Setup

1. After flashing, ESP32 creates WiFi AP: **squeezelite-XXXX**
2. Connect to this AP (no password)
3. Open **http://192.168.4.1** in browser
4. Configure your WiFi network
5. Configure LMS server address
6. Reboot and enjoy! 🎶

---

## 🔊 Hardware Configuration

### Default Pin Configuration

| Function | GPIO | Notes |
|----------|------|-------|
| SPDIF BCK | 38 | Clock |
| SPDIF WS | 39 | Word Select |
| SPDIF DO | 40 | Data Out |
| Status LED | 48 | Green LED |

### Supported DACs
- Internal DAC
- External I2S DACs (PCM5102, ES9018, TAS5713, etc.)
- SPDIF output

---

## 📋 Features & Services

Configure in Web UI → System → Services:

| Service | Description |
|---------|-------------|
| **Squeezelite** | LMS player (always enabled) |
| **Spotify (cspot)** | Spotify Connect receiver |
| **AirPlay** | Apple AirPlay receiver |
| **BT Speaker** | Bluetooth A2DP sink |
| **Telnet** | Remote logging |

---

## 🛠️ Troubleshooting

### Device not booting
- Ensure firmware is flashed to **0x1D0000** (OTA_0), not 0x50000

### Configuration not saving
- This firmware includes fixes for NVS persistence
- All checkboxes properly save true/false values

### Device freezing
- Watchdog panic is enabled - device will auto-restart
- TCP keepalive enabled for stable connections

### Cannot connect to WiFi AP
- Press RESET button
- Wait 30 seconds for AP to start

---

## 📄 License

This project is based on [squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) and is licensed under GPL-3.0.

---

## 🙏 Credits

- [sle118/squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) - Original project
- [Logitech Media Server](https://github.com/Logitech/slimserver)
- [cspot](https://github.com/feelfreelinux/cspot) - Spotify Connect

---

**Last updated:** 2026-01-15  
**Firmware version:** 5.3.1  
**ESP-IDF:** v5.3.1

