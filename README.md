# CAIR — Smarter Air Purifier (ESP32-S3 + FreeRTOS)

Firmware PlatformIO untuk ESP32-S3-DevKitC-1 (+ GPIO shield khurslabs) atau WEMOS D1 R32:
baca PM2.5/PM10 (SDS011, UART1), suhu/RH (AHT21) dan eCO2/TVOC (ENS160) via I2C, atur kipas AUTO / POT / FIXED,
tampil di OLED 0.96", RGB onboard, app RemoteXY (BLE) dan dashboard web (WiFi). Console + log di UART0 (port USB "UART").
Selama sensor belum ada, `tools/sds011_sim.py` berperan sebagai SDS011.

## Struktur

```
platformio.ini
include/config.h       pin map + default (EDIT DI SINI)
include/app.h          state bersama, mutex, queue
include/settings.h     struct konfigurasi (disimpan di NVS)
include/sds011.h       driver protokol SDS011
src/main.cpp           buat task FreeRTOS
src/sensor_task.cpp    UART1 <-> SDS011, moving average
src/fan_task.cpp       logika kipas, PWM 25 kHz, LED, tombol mode
src/display_task.cpp   OLED SSD1306
src/console_task.cpp   CLI di UART0
src/env_task.cpp       driver ENS160 + AHT21 (tanpa library)
src/remotexy_task.cpp  app RemoteXY via BLE
src/web_task.cpp       WiFi AP/STA, REST API, riwayat, CSV
src/settings.cpp       NVS load/save
include/remotexy_gui.h CONF + struct dari editor RemoteXY
include/web_index.h    AUTO-GENERATED dari web/index.html
web/index.html         tampilan dashboard web (edit di sini)
tools/embed_web.py     web/index.html -> include/web_index.h (otomatis saat build)
tools/sds011_sim.py    simulator SDS011
```

## Dua board, satu kode

| Environment | Board | Build & upload |
|---|---|---|
| `esp32-s3-devkitc-1` (default) | ESP32-S3-DevKitC-1 N16R8 + shield khurslabs | `pio run -e esp32-s3-devkitc-1 -t upload` |
| `d1_r32` | WEMOS D1 R32 (ESP32-WROOM-32) | `pio run -e d1_r32 -t upload` |

Di VS Code pilih environment lewat status bar bawah (`env:...`). Peta pin tiap board ada di `include/config.h`
(blok `#if defined(BOARD_D1R32)`); semua logika lain sama.

### Wiring WEMOS D1 R32

| Fungsi | GPIO | Label header |
|---|---|---|
| Log / console / upload | 1 / 3 | USB (CH340) |
| SDS011 TX → ESP RX | 16 | D5 |
| ESP TX → SDS011 RX | 17 | D4 |
| I2C SDA / SCL | 21 / 22 | SDA / SCL |
| PWM kipas | 25 | D3 |
| Relay daya kipas (modul eksternal) | 26 | D2 |
| TACH kipas + pull-up 10k ke 3V3 | 27 | D6 |
| Potensio (ADC1) | 34 | A3 |
| Tombol mode (ke GND) | 14 | D7 |
| LED indikator | 2 | LED biru onboard |

Catatan D1 R32:
- Shield khurslabs tidak bisa dipasang: pakai modul relay 5 V + adaptor 12 V terpisah untuk kipas, GND disatukan.
  Modul relay "low-level trigger" → set `RELAY_ACTIVE_HIGH 0`.
- LED satu warna memakai pola kedip: singkat tiap 3 s = aman, 1 Hz = mulai naik, 2.5 Hz = bahaya,
  menyala terus = sangat bahaya, kedip ganda = sensor PM error, sangat cepat = kipas macet.
  Punya WS2812? sambungkan DIN ke GPIO2 (atau pin lain) dan set `LED_WS2812 1`.
- RAM lebih kecil (tanpa PSRAM). Perhatikan `heap free` di log boot; bila < ~40 KB, matikan BLE atau web.

## Wiring ESP32-S3 (default `config.h`)

| Fungsi | GPIO | Catatan |
|---|---|---|
| UART0 console / log | 43 / 44 | port USB "UART" (juga untuk upload), 115200 |
| UART1 RX ← TX SDS011 / TX USB-TTL | 18 | simulator via USB-TTL 3.3 V, 9600 |
| UART1 TX → RX SDS011 / RX USB-TTL | 17 | |
| I2C SDA / SCL | 8 / 9 | OLED 0x3C, AHT21 0x38, ENS160 0x53 (bus sama, paralel) |
| PWM kipas | 5 | ke pin PWM (biru) kipas 4-pin |
| TACH kipas | 7 | kabel TACH (hijau/kuning) + pull-up 10k ke 3V3 |
| Relay daya kipas | 47 | Relay1 shield, memutus +12 V kipas |
| Potensio (wiper) | 4 | ujung potensio ke 3V3 dan GND |
| RGB onboard | 48 | WS2812 di board (v1.1 = 38); bila mati, jembatani pad solder "RGB" |
| Tombol mode | 0 | tombol BOOT di board |

Pinout shield khurslabs tidak saya verifikasi — cocokkan label header shield lalu ubah `config.h` bila beda.
SDS011 asli: TX sensor → GPIO18, RX sensor → GPIO17, VCC 5 V, GND bersama (logika SDS011 3.3 V, aman langsung).

Kipas DC 2-wire: PWM → MOSFET (mis. AO3400/IRLZ44N) low-side + dioda flyback di kipas. Kipas 4-wire: PWM langsung ke pin PWM kipas, GND bersama.

## Mode kipas

- **AUTO** — PM2.5 rata-rata → kategori → duty:

  | PM2.5 (µg/m³) | Kategori | Duty default |
  |---|---|---|
  | < 15 | AMAN | 30 % |
  | 15 – 20 | CENDERUNG TIDAK AMAN | 50 % |
  | 20 – 30 | BAHAYA | 75 % |
  | ≥ 30 | SANGAT BAHAYA | 100 % |

  Turun kategori butuh PM2.5 < batas − histeresis. Sensor error → `fault` duty (50 %).
- **POT** — potensio 0–100 % (deadband 3 %).
- **FIXED** — duty tetap (60 %).

Ganti mode: tekan tombol BOOT (langsung tersimpan) atau `mode ...` di console.
Perubahan kecepatan dibatasi `slew` %/detik; start dari 0 diberi kick 100 % selama 300 ms.

RGB onboard: hijau / kuning / oranye / merah kedip = level terburuk dari kategori PM2.5 dan level eCO2
(800 / 1000 / 1500 ppm). Ungu kedip = sensor PM error, merah-biru cepat = kipas macet.
eCO2 tidak memengaruhi kecepatan kipas (filter tidak menurunkan CO2) — hanya peringatan & saran ventilasi.

## Dashboard web (WiFi)

- Hotspot alat selalu aktif: **CAIR-XXXX**, password `cair1234` → halaman terbuka otomatis (captive portal) atau buka `http://192.168.4.1`.
- Sambungkan ke router lewat menu *WiFi & koneksi* di halaman, atau console `wifi <ssid> <password>`; lalu buka `http://cair.local` atau IP yang tampil di log.
- Isi: PM2.5/PM10, suhu, RH, eCO2, TVOC, AQI, saran otomatis, kontrol kipas (Otomatis/Manual/Potensio + slider), grafik 2 jam, unduh CSV, pengaturan batas & duty.
- Saat terhubung ke router, waktu disinkron NTP (WIB) sehingga CSV berisi kolom waktu.
- Ubah tampilan: edit `web/index.html`, build ulang (header gzip dibuat otomatis).

API: `GET /api/status`, `POST /api/mode` (mode=auto|fixed|pot), `POST /api/fan` (duty=0..100),
`GET|POST /api/config`, `GET /api/history`, `GET /api/history.csv`, `GET /api/wifi/scan`, `POST /api/wifi`.

## Build & upload

```
pio run -t upload --upload-port COM5      # port USB "UART"
pio device monitor -p COM5                 # port yang sama, 115200
```

Simulator jalan di port USB-TTL (UART1), jadi tidak bentrok dengan upload/monitor.

## Simulator

```
pip install -r tools/requirements.txt
python tools/sds011_sim.py --list
python tools/sds011_sim.py --port COM7 --profile sine --base 10 --amp 30 --period 120
```

Profil: `steady`, `sine`, `ramp`, `spike`, `walk`. Saat jalan, ketik: `set 25`, `burst`, `stop` (uji timeout), `bad` (frame rusak), `profile spike`, `status`, `quit`.
Simulator menjawab perintah SDS011 (reporting mode, query, sleep/work, period, id, firmware), jadi `sds query 10` / `sds sleep` di console firmware ikut teruji.


## Console (UART0)

```
help | status | cfg | env
wifi | wifi <ssid> [password] | wifi forget
mode auto|pot|fixed
fixed 60 | fault 50
th 15 20 30
duty 30 50 75 100
hyst 1 | slew 25 | avg 5 | dead 3
sds active | sds query 10 | sds sleep | sds wake | sds read
log on 2000 | log off
save | load | defaults
```

Setting berlaku langsung; ketik `save` agar tetap setelah reset.
