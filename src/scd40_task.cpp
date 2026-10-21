// =====================================================================
//  SCD40 (Sensirion) - CO2 NDIR asli, I2C 0x62, bus sama dgn OLED/ENS160/AHT21
//  Driver minimal tanpa library:
//    start_periodic_measurement 0x21B1   (data baru tiap 5 s)
//    stop_periodic_measurement  0x3F86   (tunggu 500 ms)
//    get_data_ready_status      0xE4B8 -> 1 word, siap bila (word & 0x07FF) != 0
//    read_measurement           0xEC05 -> 3 word: CO2 ppm, T, RH
//  Tiap word 2 byte + CRC8 (poly 0x31, init 0xFF).
//
//  Hasil ditulis ke variabel yang SAMA dengan sensor lama (g_env.eco2), sehingga
//  OLED, LED, web, RemoteXY dan riwayat otomatis memakai CO2 SCD40.
//  Suhu/RH SCD40 hanya dipakai bila AHT21 tidak ada (SCD40 sedikit memanas sendiri).
// =====================================================================
#include "app.h"
#include "log.h"
#include <Wire.h>

#if ENABLE_SCD40

static uint8_t scdCrc(const uint8_t* d, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

static bool scdCmd(uint16_t cmd) {
  I2CLock l;
  Wire.beginTransmission(SCD40_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)cmd);
  return Wire.endTransmission() == 0;
}

// baca n word (masing-masing 2 byte + CRC) setelah perintah dikirim
static bool scdReadWords(uint16_t* w, int n) {
  uint8_t buf[9];
  int len = n * 3;
  {
    I2CLock l;
    if (Wire.requestFrom((uint8_t)SCD40_ADDR, (uint8_t)len) != len) return false;
    for (int i = 0; i < len; i++) buf[i] = Wire.read();
  }
  for (int i = 0; i < n; i++) {
    if (scdCrc(&buf[i * 3], 2) != buf[i * 3 + 2]) return false;
    w[i] = ((uint16_t)buf[i * 3] << 8) | buf[i * 3 + 1];
  }
  return true;
}

static bool scdInit() {
  // bila MCU reset saat sensor masih mengukur, perintah lain ditolak -> stop dulu
  if (!scdCmd(0x3F86)) return false;
  vTaskDelay(pdMS_TO_TICKS(500));
  if (!scdCmd(0x21B1)) return false;
  LOG("[scd] SCD40 di 0x%02X, periodic measurement (5 s) dimulai", SCD40_ADDR);
  return true;
}

// return: -1 error, 0 belum ada data, 1 data baru
static int scdRead(uint16_t& co2, float& t, float& rh) {
  uint16_t w[3];
  if (!scdCmd(0xE4B8)) return -1;
  vTaskDelay(pdMS_TO_TICKS(2));
  if (!scdReadWords(w, 1)) return -1;
  if ((w[0] & 0x07FF) == 0) return 0;
  if (!scdCmd(0xEC05)) return -1;
  vTaskDelay(pdMS_TO_TICKS(2));
  if (!scdReadWords(w, 3)) return -1;
  co2 = w[0];
  t   = -45.0f + 175.0f * w[1] / 65535.0f;
  rh  = 100.0f * w[2] / 65535.0f;
  return 1;
}

void scd40Task(void*) {
  vTaskDelay(pdMS_TO_TICKS(1200));           // SCD40 butuh ~1 s setelah power-on
  bool ok = scdInit();
  if (!ok) LOG("[scd] SCD40 TIDAK TERDETEKSI di 0x%02X (CO2 pakai ENS160 bila ada)", SCD40_ADDR);

  uint32_t lastRetry = millis(), lastData = millis();
  uint8_t  fail = 0;
  bool     first = true;
  TickType_t last = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    uint32_t now = millis();

    if (!ok) {
      if (now - lastRetry > 10000) {
        lastRetry = now;
        if ((ok = scdInit())) { fail = 0; first = true; lastData = now; }
      }
      continue;
    }

    uint16_t co2 = 0; float t = NAN, rh = NAN;
    int r = scdRead(co2, t, rh);
    if (r < 0) {
      if (++fail >= 5) {
        ok = false; fail = 0;
        { Lock l; g_env.scdOk = false; }
        LOG("[scd] SCD40 gagal dibaca 5x, ditandai OFFLINE");
      }
      continue;
    }
    fail = 0;
    if (r == 0) {
      if (now - lastData > 30000) {           // tidak ada data 30 s -> mulai ulang
        LOG("[scd] tidak ada data 30 s, inisialisasi ulang");
        ok = false; lastRetry = 0;
        { Lock l; g_env.scdOk = false; }
      }
      continue;
    }
    lastData = now;
    if (co2 == 0) continue;                   // pembacaan pertama kadang 0

    if (first) { first = false; LOG("[scd] ONLINE, CO2=%u ppm T=%.1fC RH=%.0f%%", co2, t, rh); }
    LOGV("[scd] CO2=%u ppm T=%.1fC RH=%.0f%%", co2, t, rh);

    Lock l;
    g_env.scdOk  = true;
    g_env.scdMs  = now;
    g_env.scdT   = t;
    g_env.scdRh  = rh;
    g_env.eco2   = co2;                       // variabel sama dengan sensor lama
    g_env.co2Ms  = now;
    g_env.co2Src = CO2_SRC_SCD40;
    // suhu/RH: AHT21 diutamakan; SCD40 hanya cadangan
    bool ahtActive = g_env.thSrc == TH_SRC_AHT21 && (now - g_env.thMs) < 5000;
    if (!ahtActive) { g_env.t = t; g_env.rh = rh; g_env.thMs = now; g_env.thSrc = TH_SRC_SCD40; }
  }
}

#endif  // ENABLE_SCD40
