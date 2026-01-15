# ESP Web Tools - Web Flash

## English

### How to Flash
1. Open index.html in Chrome or Edge browser
2. Connect ESP32-S3 via USB cable
3. Click **Connect**
4. Select the COM port of your device
5. Click **Install**
6. Wait for the process to complete

### Requirements
- Chrome or Edge browser (Firefox not supported)
- USB cable connected to ESP32-S3
- Modern computer with WebSerial support

### After Flashing
1. ESP32 will create WiFi hotspot: squeezelite-XXXX
2. Connect to this network (no password)
3. Open http://192.168.4.1
4. Configure your WiFi and LMS server

---

## Polski

### Jak Wgrac Firmware
1. Otworz plik index.html w przegladarce Chrome lub Edge
2. Podlacz ESP32-S3 kablem USB
3. Kliknij **Connect**
4. Wybierz port COM swojego urzadzenia
5. Kliknij **Install**
6. Poczekaj na zakonczenie procesu

### Wymagania
- Przegladarka Chrome lub Edge (Firefox nie obslugiwany)
- Kabel USB podlaczony do ESP32-S3
- Nowoczesny komputer z obsluga WebSerial

### Po Wgraniu
1. ESP32 utworzy punkt dostepowy WiFi: squeezelite-XXXX
2. Polacz sie z ta siecia (bez hasla)
3. Otworz http://192.168.4.1
4. Skonfiguruj WiFi i serwer LMS

---

## Files / Pliki

| File | Address | Description |
|------|---------|-------------|
| bootloader.bin | 0x0 | Bootloader |
| partition-table.bin | 0x8000 | Partition table |
| ota_data_initial.bin | 0x49000 | OTA boot data |
| squeezelite.bin | 0x1D0000 | Main application |
