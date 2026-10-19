#pragma once
// =====================================================================
//  CAIR - konfigurasi hardware & default
//  Pin map di bawah = default DevKitC-1. Sesuaikan dengan header di
//  GPIO shield khurslabs bila label pin-nya berbeda.
// =====================================================================

// ---- UART0 : serial monitor / console (port USB "UART", sama dgn upload) ----
#define PIN_LOG_RX        44      // U0RXD
#define PIN_LOG_TX        43      // U0TXD
#define LOG_BAUD          115200
// dgn CDC_ON_BOOT=0, `Serial` = UART0 (Serial0 tidak ada di core lama)
#if ARDUINO_USB_CDC_ON_BOOT
  #define LOG_SERIAL      Serial0
#else
  #define LOG_SERIAL      Serial
#endif

// ---- UART1 : SDS011 (atau simulator via USB-TTL 3.3V) ----
#define PIN_SDS_RX        18      // <- TX sensor / TX USB-TTL
#define PIN_SDS_TX        17      // -> RX sensor / RX USB-TTL
#define SDS_BAUD          9600
#define SDS_SERIAL        Serial1
#define LOG_VERBOSE       1       // 1 = log tiap frame sensor/reply, 0 = event penting saja

// ---- I2C bus: OLED 0.96" SSD1306 128x64 + ENS160 (0x53) + AHT21 (0x38) ----
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9
#define OLED_ADDR         0x3C
#define OLED_W            128
#define OLED_H            64

// ---- Kipas (PWM -> gate MOSFET / pin PWM kipas 4-wire) ----
#define PIN_FAN_PWM       5
#define FAN_PWM_FREQ      25000   // 25 kHz (standar kipas PC, tak terdengar)
#define FAN_PWM_RES       10      // bit
#define FAN_PWM_CH        0       // dipakai di core Arduino 2.x
#define FAN_PWM_INVERT    0       // 1 jika driver membalik logika
#define FAN_KICK_MS       300     // dorongan 100% saat start dari 0 (0 = off)

// ---- Relay daya kipas 12V (Relay1 shield khurslabs) ----
#define FAN_USE_RELAY     1       // 0 = tanpa relay
#define PIN_FAN_RELAY     47      // Relay1 = GPIO47, Relay2 = GPIO21
#define RELAY_ACTIVE_HIGH 1       // 0 jika relay ON saat pin LOW
#define FAN_RELAY_OFF_MS  5000    // relay OFF setelah kipas 0% selama ini (anti klik)

// ---- Tachometer kipas 4-pin (kabel TACH, open-collector) ----
#define PIN_FAN_TACH      7       // WAJIB pull-up 10k ke 3V3 (jangan ke 5V/12V)
#define FAN_TACH_PPR      2       // pulsa per putaran (standar kipas PC = 2)
#define FAN_TACH_MIN_US   2000    // filter glitch: abaikan pulsa < 2 ms (maks ~15000 rpm)
#define FAN_STALL_MIN_PCT 20      // >= duty ini tapi rpm 0 selama 3 s -> peringatan macet

// ---- Input manual & indikator ----
#define PIN_POT           4       // ADC1_CH3, wiper potensio (0-3.3V)
#define PIN_BTN_MODE      0       // tombol BOOT: ganti mode AUTO/POT/FIXED
#define PIN_RGB           48      // RGB onboard WS2812: v1.0 = 48, v1.1 = 38
#define RGB_BRIGHTNESS    40      // 0-255 (LED onboard sangat terang)

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
