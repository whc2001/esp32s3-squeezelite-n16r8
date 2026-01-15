# 🎵 Squeezelite ESP32-S3 Odtwarzacz Audio

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.1-blue)](https://docs.espressif.com/projects/esp-idf/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-green)](https://www.espressif.com/en/products/socs/esp32-s3)
[![License](https://img.shields.io/badge/License-GPL--3.0-orange)](LICENSE)

**Squeezelite** to wysokiej jakości odtwarzacz audio dla **ESP32-S3** z obsługą:
- 🎧 **Logitech Media Server (LMS)** - strumieniowanie
- 🎵 **Spotify Connect** (cspot)
- 🍎 **AirPlay** - odbiornik
- 📻 **Bluetooth A2DP** - głośnik BT
- 🔊 **SPDIF / I2S / DAC** - wyjście audio

---

## 🚀 Szybki Start - Wgrywanie Gotowego Firmware

### Opcja 1: Web Flasher (Najłatwiejsza)
1. Wejdź na [web.esphome.io](https://web.esphome.io)
2. Podłącz ESP32-S3 przez USB
3. Wybierz plik `esphome_flash/squeezelite_full.bin`
4. Kliknij **INSTALL**

### Opcja 2: ESP Web Tools
Otwórz `web_flash/index.html` w Chrome/Edge i postępuj zgodnie z instrukcjami.

### Opcja 3: Linia poleceń
```bash
esptool.py --chip esp32s3 -p COM24 -b 460800 write_flash 0x0 firmware/squeezelite_full.bin
```

---

## 📦 Gotowy Firmware

| Plik | Opis | Rozmiar |
|------|------|---------|
| `firmware/squeezelite_full.bin` | Pełny obraz (flash na 0x0) | ~4.2 MB |
| `firmware/squeezelite.bin` | Tylko aplikacja (flash na 0x1D0000) | ~2.3 MB |
| `firmware/bootloader.bin` | Bootloader (0x0) | ~21 KB |
| `firmware/partition-table.bin` | Tablica partycji (0x8000) | ~3 KB |
| `firmware/ota_data_initial.bin` | Dane OTA (0x49000) | ~8 KB |

---

## 🔧 Budowanie ze Źródeł

### Wymagania
- **ESP-IDF v5.3.1** lub nowszy
- **Python 3.8+**
- **Git**

### Kroki budowania

```bash
# Sklonuj repozytorium
git clone https://github.com/YOUR_USER/squeezelite-esp32s3.git
cd squeezelite-esp32s3/source

# Ustaw środowisko ESP-IDF
. $IDF_PATH/export.sh

# Zbuduj
idf.py build

# Wgraj (pełny flash)
idf.py -p /dev/ttyUSB0 flash

# Lub wgraj tylko aplikację do OTA_0
esptool.py --chip esp32s3 -p /dev/ttyUSB0 write_flash 0x1D0000 build/squeezelite.bin
```

### Windows (PowerShell)
```powershell
# Użyj dostarczonych skryptów
.\build_with_env.ps1
.\flash_with_env.ps1
```

---

## ⚡ Adresy Flash (Układ Partycji)

| Partycja | Adres | Rozmiar | Opis |
|----------|-------|---------|------|
| bootloader | 0x0 | 32 KB | Bootloader ESP-IDF |
| partition-table | 0x8000 | 4 KB | Tablica partycji |
| nvs | 0x9000 | 256 KB | Konfiguracja NVS |
| otadata | 0x49000 | 8 KB | Wybór bootu OTA |
| phy_init | 0x4B000 | 4 KB | Kalibracja PHY |
| recovery | 0x50000 | 1.5 MB | Aplikacja fabryczna (nieużywana) |
| **ota_0** | **0x1D0000** | 12.3 MB | **Główna aplikacja** |
| settings | 0xE00000 | 2 MB | Dodatkowe ustawienia |

⚠️ **Ważne:** Wgrywaj firmware na **OTA_0 (0x1D0000)**, nie na partycję recovery!

---

## 🌐 Pierwsza Konfiguracja

1. Po wgraniu, ESP32 tworzy punkt dostępowy WiFi: **squeezelite-XXXX**
2. Połącz się z tym AP (bez hasła)
3. Otwórz **http://192.168.4.1** w przeglądarce
4. Skonfiguruj swoją sieć WiFi
5. Skonfiguruj adres serwera LMS
6. Zrestartuj urządzenie i ciesz się muzyką! 🎶

---

## 🔊 Konfiguracja Sprzętowa

### Domyślna Konfiguracja Pinów

| Funkcja | GPIO | Uwagi |
|---------|------|-------|
| SPDIF BCK | 38 | Zegar |
| SPDIF WS | 39 | Word Select |
| SPDIF DO | 40 | Dane wyjściowe |
| LED Status | 48 | Zielona dioda |

### Obsługiwane Przetworniki DAC
- Wewnętrzny DAC
- Zewnętrzne przetworniki I2S (PCM5102, ES9018, TAS5713, itp.)
- Wyjście SPDIF

---

## 📋 Funkcje i Usługi

Konfiguracja w Web UI → System → Usługi:

| Usługa | Opis |
|--------|------|
| **Squeezelite** | Odtwarzacz LMS (zawsze włączony) |
| **Spotify (cspot)** | Odbiornik Spotify Connect |
| **AirPlay** | Odbiornik Apple AirPlay |
| **BT Speaker** | Głośnik Bluetooth A2DP |
| **Telnet** | Zdalne logowanie |

---

## 🛠️ Rozwiązywanie Problemów

### Urządzenie się nie uruchamia
- Upewnij się, że firmware jest wgrany na **0x1D0000** (OTA_0), nie na 0x50000

### Konfiguracja się nie zapisuje
- Ten firmware zawiera poprawki dla trwałości NVS
- Wszystkie checkboxy poprawnie zapisują wartości true/false

### Urządzenie zawiesza się
- Watchdog panic jest włączony - urządzenie automatycznie się zrestartuje
- TCP keepalive włączony dla stabilnych połączeń

### Nie można połączyć się z WiFi AP
- Naciśnij przycisk RESET
- Poczekaj 30 sekund na uruchomienie AP

---

## 📁 Struktura Projektu

```
squeezelite-esp32s3/
├── source/                    # Pełny kod źródłowy
│   ├── main/                  # Główna aplikacja
│   ├── components/            # Wszystkie komponenty
│   ├── managed_components/    # Komponenty zarządzane ESP-IDF
│   ├── CMakeLists.txt        # Konfiguracja budowania
│   ├── sdkconfig             # Konfiguracja ESP-IDF
│   └── partitions.csv        # Tablica partycji
├── web_flash/                 # Pakiet ESP Web Tools
├── esphome_flash/            # Flash przez ESPHome web
├── firmware/                  # Gotowe pliki binarne
├── README.md                  # Dokumentacja (EN)
└── README.pl.md              # Dokumentacja (PL)
```

---

## 📄 Licencja

Ten projekt jest oparty na [squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) i jest licencjonowany na GPL-3.0.

---

## 🙏 Podziękowania

- [sle118/squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) - Oryginalny projekt
- [Logitech Media Server](https://github.com/Logitech/slimserver)
- [cspot](https://github.com/feelfreelinux/cspot) - Spotify Connect

---

**Ostatnia aktualizacja:** 2026-01-15  
**Wersja firmware:** 5.3.1  
**ESP-IDF:** v5.3.1
