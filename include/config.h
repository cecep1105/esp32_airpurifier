#pragma once
// =====================================================================
//  CAIR - konfigurasi hardware & default
//  Peta pin dipilih otomatis per environment PlatformIO:
//    esp32-s3-devkitc-1 : ESP32-S3-DevKitC-1 + GPIO shield khurslabs
//    d1_r32             : WEMOS D1 R32 (build flag -DBOARD_D1R32)
//  Cocokkan dengan label header board/shield bila berbeda.
// =====================================================================

#if defined(BOARD_D1R32)
// ===================== WEMOS D1 R32 (ESP32-WROOM-32) ==================
//  Label header D1 R32 -> GPIO:
//   D2=26 D3=25 D4=17 D5=16 D6=27 D7=14 D8=12 D9=13 D10=5 D11=23 D12=19 D13=18
//   A0=2 A1=4 A2=35 A3=34 A4=36 A5=39   SDA=21 SCL=22
//  Hindari: GPIO6-11 (flash), ADC2 (GPIO0/2/4/12-15/25-27) untuk analogRead saat WiFi aktif,
//           GPIO12 sebagai input saat boot (strapping tegangan flash).
  #define BOARD_NAME        "WEMOS D1 R32"
  #define PIN_LOG_RX        3       // U0RXD (USB CH340)
  #define PIN_LOG_TX        1       // U0TXD
  #define PIN_SDS_RX        16      // D5  <- TX sensor / TX USB-TTL
  #define PIN_SDS_TX        17      // D4  -> RX sensor / RX USB-TTL
  #define SDS_SERIAL        Serial2 // pin bawaan UART1 (9/10) dipakai flash
  #define PIN_ZH_RX         19      // D12 <- TX ZH03B (PM2.5 luar)
  #define PIN_ZH_TX         23      // D11 -> RX ZH03B
  #define ZH_SERIAL         Serial1 // UART1 dipindah ke pin bebas
  #define PIN_I2C_SDA       21      // header SDA
  #define PIN_I2C_SCL       22      // header SCL
  #define PIN_FAN_PWM       25      // D3
  #define PIN_FAN_RELAY     26      // D2 -> IN modul relay 5V (eksternal)
  #define PIN_FAN_TACH      27      // D6 + pull-up 10k ke 3V3
  #define PIN_POT           34      // A3 (ADC1, input-only) - aman saat WiFi aktif
  #define PIN_BTN_MODE      14      // D7, tombol eksternal ke GND (D1 R32 tanpa tombol BOOT)
  #define PIN_RGB           2       // LED biru onboard (atau DIN WS2812 eksternal)
  #define LED_WS2812        0       // 0 = LED satu warna (pola kedip), 1 = WS2812
  #define RELAY_ACTIVE_HIGH 1       // modul relay "low-level trigger": set 0
  // RAM ESP32 klasik tidak cukup untuk BLE + WiFi bersamaan -> RemoteXY dimatikan
  #ifndef ENABLE_REMOTEXY
    #define ENABLE_REMOTEXY 0
  #endif
#else
// ================= ESP32-S3-DevKitC-1 + shield khurslabs ===============
  #define BOARD_NAME        "ESP32-S3-DevKitC-1"
  #define PIN_LOG_RX        44      // U0RXD (port USB "UART", sama dgn upload)
  #define PIN_LOG_TX        43      // U0TXD
  #define PIN_SDS_RX        18      // <- TX sensor / TX USB-TTL
  #define PIN_SDS_TX        17      // -> RX sensor / RX USB-TTL
  #define SDS_SERIAL        Serial1
  #define PIN_ZH_RX         16      // header UART shield "16/RX" <- TX ZH03B (PM2.5 luar)
  #define PIN_ZH_TX         15      // header UART shield "15/TX" -> RX ZH03B
  #define ZH_SERIAL         Serial2
  #define PIN_I2C_SDA       8
  #define PIN_I2C_SCL       9
  #define PIN_FAN_PWM       5
  #define PIN_FAN_RELAY     47      // Relay1 shield = GPIO47, Relay2 = GPIO21
  #define PIN_FAN_TACH      7       // + pull-up 10k ke 3V3
  #define PIN_POT           4       // ADC1_CH3
  #define PIN_BTN_MODE      0       // tombol BOOT
  #define PIN_RGB           48      // RGB onboard WS2812: v1.0 = 48, v1.1 = 38
  #define LED_WS2812        1
  #define RELAY_ACTIVE_HIGH 1       // 0 jika relay ON saat pin LOW
#endif

// ---- Fitur konektivitas (default per board di atas; bisa ditimpa lewat
//      build_flags di platformio.ini, mis. -DENABLE_REMOTEXY=0) ----
#ifndef ENABLE_REMOTEXY
  #define ENABLE_REMOTEXY 1         // app RemoteXY via BLE
#endif
#ifndef ENABLE_WEB
  #define ENABLE_WEB      1         // WiFi AP/STA + dashboard web
#endif
#ifndef ENABLE_ZH03B
  #define ENABLE_ZH03B    1         // PM2.5 luar ruangan (UART)
#endif
#ifndef ENABLE_SCD40
  #define ENABLE_SCD40    1         // CO2 NDIR (I2C 0x62); ENS160 jadi cadangan
#endif

// ---- UART0 : serial monitor / console ----
#define LOG_BAUD          115200
// dgn CDC_ON_BOOT=0 (S3) atau ESP32 klasik, `Serial` = UART0
#if ARDUINO_USB_CDC_ON_BOOT
  #define LOG_SERIAL      Serial0
#else
  #define LOG_SERIAL      Serial
#endif
#define LOG_VERBOSE       1       // 1 = log tiap frame sensor/reply, 0 = event penting saja

// ---- SDS011 (PM dalam ruangan) ----
#define SDS_BAUD          9600

// ---- ZH03B (PM luar ruangan), mode initiative upload 1 s ----
#define ZH_BAUD           9600
#define OUT_TIMEOUT_MS    5000    // tanpa frame selama ini = sensor luar offline
#define OUT_EFF_MIN_PM    5.0f    // efektivitas (%) hanya dihitung bila PM luar >= ini

// ---- SCD40 (CO2 NDIR) ----
#define SCD40_ADDR        0x62

// ---- I2C bus: OLED 0.96" SSD1306 128x64 + ENS160 (0x53) + AHT21 (0x38) ----
#define OLED_ADDR         0x3C
#define OLED_W            128
#define OLED_H            64

// ---- Kipas 4-pin (PWM langsung ke pin PWM kipas) ----
#define FAN_PWM_FREQ      25000   // 25 kHz (standar kipas PC, tak terdengar)
#define FAN_PWM_RES       10      // bit
#define FAN_PWM_CH        0       // dipakai di core Arduino 2.x
#define FAN_PWM_INVERT    0       // 1 jika driver membalik logika
#define FAN_KICK_MS       300     // dorongan 100% saat start dari 0 (0 = off)

// ---- Relay daya kipas 12V ----
#define FAN_USE_RELAY     1       // 0 = tanpa relay
#define FAN_RELAY_OFF_MS  5000    // relay OFF setelah kipas 0% selama ini (anti klik)

// ---- Tachometer kipas 4-pin (kabel TACH, open-collector) ----
#define FAN_TACH_PPR      2       // pulsa per putaran (standar kipas PC = 2)
#define FAN_TACH_MIN_US   2000    // filter glitch: abaikan pulsa < 2 ms (maks ~15000 rpm)
#define FAN_STALL_MIN_PCT 20      // >= duty ini tapi rpm 0 selama 3 s -> peringatan macet

// ---- Indikator ----
#define RGB_BRIGHTNESS    40      // 0-255, hanya untuk WS2812 (LED onboard sangat terang)

// ---- eCO2 (ENS160) : level untuk peringatan & saran ventilasi ----
#define CO2_TH1           800     // >= : SEDANG
#define CO2_TH2           1000    // >= : BURUK  (saran buka ventilasi)
#define CO2_TH3           1500    // >= : SGT BURUK

// ---- RemoteXY (BLE) ----
#define RXY_BLE_NAME      "CAIR"  // nama BLE yang muncul di app
// #define RXY_PASSWORD   "1234"  // aktifkan bila ingin pakai password
#define RXY_FAN_STEP      5       // % per tekan tombol +/-
#define RXY_BAR_FULL_PM   50.0f   // PM2.5 (ug/m3) = circular bar 100%

// ---- WiFi + web dashboard ----
#define WIFI_HOSTNAME     "cair"      // http://cair.local
#define WIFI_AP_ALWAYS    1           // 1 = AP selalu aktif (bisa diakses tanpa router)
#define WIFI_AP_PREFIX    "CAIR-"     // SSID AP = CAIR-XXXX (4 digit MAC)
#define WIFI_AP_PASS      "cair1234"  // min 8 karakter
#define WEB_PORT          80
#define HIST_INTERVAL_S   30          // sampling riwayat (detik)
#define HIST_LEN          240         // 240 x 30 s = 2 jam

// ---- Sensor ----
#define SENSOR_TIMEOUT_MS 5000    // tanpa frame selama ini = sensor fault
#define AVG_MAX           30      // panjang maksimum moving average

// ---- Default setting (bisa diubah via console, disimpan di NVS) ----
#define DEF_MODE          0       // 0=AUTO 1=POT 2=FIXED
#define DEF_TH1           15.0f   // < TH1        : AMAN
#define DEF_TH2           20.0f   // TH1..TH2     : CENDERUNG TIDAK AMAN
#define DEF_TH3           30.0f   // TH2..TH3     : BAHAYA, >= TH3 : SANGAT BAHAYA
#define DEF_DUTY0         30      // % kipas per kategori
#define DEF_DUTY1         50
#define DEF_DUTY2         75
#define DEF_DUTY3         100
#define DEF_FIXED_DUTY    60
#define DEF_FAULT_DUTY    50      // mode AUTO tapi sensor mati
#define DEF_HYST          1.0f    // ug/m3, cegah kipas naik-turun di batas
#define DEF_SLEW          25      // %/detik perubahan kecepatan (0 = langsung)
#define DEF_AVG           5       // jumlah sampel moving average
#define DEF_SDS_QUERY     0       // 0 = active (1 data/detik), 1 = query mode
#define DEF_SDS_QINT_S    10      // interval query (detik)
#define DEF_POT_DEADBAND  3       // % potensio di bawah ini = kipas mati
