# ESPHome Web Flash - Single Binary

## English

### How to Flash (Easiest Method)
1. Go to [web.esphome.io](https://web.esphome.io)
2. Connect ESP32-S3 via USB cable
3. Click **Connect** and select your device
4. Click **Install** button
5. Choose file: **squeezelite_full.bin** from this folder
6. Wait for flashing to complete

### Alternative - Command Line
```
esptool.py --chip esp32s3 -p COM24 -b 460800 write_flash 0x0 squeezelite_full.bin
```

### After Flashing
1. ESP32 will create WiFi hotspot: squeezelite-XXXX
2. Connect to this network (no password)
3. Open http://192.168.4.1
4. Configure your WiFi and LMS server

---

## Polski

### Jak Wgrac Firmware (Najlatwiejsza Metoda)
1. Wejdz na strone [web.esphome.io](https://web.esphome.io)
2. Podlacz ESP32-S3 kablem USB
3. Kliknij **Connect** i wybierz swoje urzadzenie
4. Kliknij przycisk **Install**
5. Wybierz plik: **squeezelite_full.bin** z tego folderu
6. Poczekaj na zakonczenie wgrywania

### Alternatywa - Linia polecen
```
esptool.py --chip esp32s3 -p COM24 -b 460800 write_flash 0x0 squeezelite_full.bin
```

### Po Wgraniu
1. ESP32 utworzy punkt dostepowy WiFi: squeezelite-XXXX
2. Polacz sie z ta siecia (bez hasla)
3. Otworz http://192.168.4.1
4. Skonfiguruj WiFi i serwer LMS

---

## File Info / Informacje o Pliku

| File | Size | Flash Address |
|------|------|---------------|
| squeezelite_full.bin | ~4.2 MB | 0x0 |

This file contains: bootloader + partition table + OTA data + application
Ten plik zawiera: bootloader + tablica partycji + dane OTA + aplikacja
