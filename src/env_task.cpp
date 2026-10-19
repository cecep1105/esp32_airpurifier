// =====================================================================
//  ENS160 (eCO2 / TVOC / AQI) + AHT21 (suhu / RH) di bus I2C bersama OLED
//  Driver minimal tanpa library eksternal.
//   AHT21  : 0x38, trigger 0xAC 0x33 0x00, data 7 byte (CRC8 0x31)
//   ENS160 : 0x53 (ADDR=1) / 0x52 (ADDR=0), register map datasheet ScioSense
//  Suhu & RH dari AHT21 dikirim ke ENS160 sebagai kompensasi.
// =====================================================================
#include "app.h"
#include "log.h"
#include <Wire.h>
#include <math.h>

// ---------- I2C helper (selalu dipanggil di dalam I2CLock) ----------
static bool i2cWrite(uint8_t addr, const uint8_t* d, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}
static bool i2cRead(uint8_t addr, uint8_t* d, size_t n) {
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}
static bool regWrite(uint8_t addr, uint8_t reg, const uint8_t* d, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}
static bool regRead(uint8_t addr, uint8_t reg, uint8_t* d, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  return i2cRead(addr, d, n);
}

// ---------- AHT21 ----------
static const uint8_t AHT_ADDR = 0x38;

static uint8_t ahtCrc(const uint8_t* d, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

static bool ahtInit() {
  I2CLock l;
  uint8_t st;
  if (!i2cRead(AHT_ADDR, &st, 1)) return false;
  if ((st & 0x18) != 0x18) {                 // belum terkalibrasi -> init
    const uint8_t cmd[3] = {0xBE, 0x08, 0x00};
    if (!i2cWrite(AHT_ADDR, cmd, 3)) return false;
  }
  return true;
}

static bool ahtTrigger() {
  I2CLock l;
  const uint8_t cmd[3] = {0xAC, 0x33, 0x00};
  return i2cWrite(AHT_ADDR, cmd, 3);
}

static bool ahtRead(float& t, float& rh) {
  uint8_t d[7];
  { I2CLock l; if (!i2cRead(AHT_ADDR, d, 7)) return false; }
  if (d[0] & 0x80) return false;             // masih busy
  if (ahtCrc(d, 6) != d[6]) return false;
  uint32_t hr = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t tr = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  rh = hr * 100.0f / 1048576.0f;
  t  = tr * 200.0f / 1048576.0f - 50.0f;
  return rh >= 0 && rh <= 100 && t > -40 && t < 85;
}

// ---------- ENS160 ----------
enum : uint8_t {
  ENS_PART_ID = 0x00, ENS_OPMODE = 0x10, ENS_CONFIG = 0x11, ENS_COMMAND = 0x12,
  ENS_TEMP_IN = 0x13, ENS_STATUS = 0x20,
};
enum : uint8_t { OPM_SLEEP = 0x00, OPM_IDLE = 0x01, OPM_STD = 0x02, OPM_RESET = 0xF0 };

static uint8_t s_ensAddr = 0;

static bool ensInit() {
  const uint8_t cands[2] = {0x53, 0x52};
  for (uint8_t a : cands) {
    uint8_t id[2];
    bool ok;
    { I2CLock l; ok = regRead(a, ENS_PART_ID, id, 2); }
    if (!ok) continue;
    uint16_t pid = id[0] | (id[1] << 8);
    if (pid != 0x0160) { LOG("[env] 0x%02X PART_ID=0x%04X (bukan ENS160)", a, pid); continue; }
    s_ensAddr = a;
    uint8_t v;
    { I2CLock l; v = OPM_RESET; regWrite(a, ENS_OPMODE, &v, 1); }
    vTaskDelay(pdMS_TO_TICKS(20));
    { I2CLock l; v = OPM_IDLE;  regWrite(a, ENS_OPMODE, &v, 1); }
    vTaskDelay(pdMS_TO_TICKS(20));
    { I2CLock l; v = 0x00; regWrite(a, ENS_CONFIG, &v, 1); }      // tanpa interrupt
    { I2CLock l; v = OPM_STD;  ok = regWrite(a, ENS_OPMODE, &v, 1); }
    vTaskDelay(pdMS_TO_TICKS(50));
    LOG("[env] ENS160 di 0x%02X, mode standard %s", a, ok ? "OK" : "GAGAL");
    return ok;
  }
  return false;
}

static bool ensCompensate(float t, float rh) {
  uint16_t ti = (uint16_t)lroundf((t + 273.15f) * 64.0f);
  uint16_t ri = (uint16_t)lroundf(rh * 512.0f);
  uint8_t d[4] = {(uint8_t)ti, (uint8_t)(ti >> 8), (uint8_t)ri, (uint8_t)(ri >> 8)};
  I2CLock l;
  return regWrite(s_ensAddr, ENS_TEMP_IN, d, 4);
}

// return: -1 error, 0 tidak ada data baru, 1 data baru
static int ensRead(uint8_t& validity, uint8_t& aqi, uint16_t& tvoc, uint16_t& eco2) {
  uint8_t d[6];
  { I2CLock l; if (!regRead(s_ensAddr, ENS_STATUS, d, 6)) return -1; }
  if (d[0] & 0x40) return -1;                // STATER: error
  validity = (d[0] >> 2) & 0x03;
  if (!(d[0] & 0x02)) return 0;              // NEWDAT belum ada
  aqi  = d[1] & 0x07;
  tvoc = d[2] | (d[3] << 8);
  eco2 = d[4] | (d[5] << 8);
  return 1;
}

// ---------- level eCO2 ----------
static const char* CO2_NAMES[4] = {"BAIK", "SEDANG", "BURUK", "SGT BURUK"};
uint8_t co2Level(uint16_t ppm) {
  if (ppm >= CO2_TH3) return 3;
  if (ppm >= CO2_TH2) return 2;
  if (ppm >= CO2_TH1) return 1;
  return 0;
}
const char* co2LevelName(uint8_t l) { return l < 4 ? CO2_NAMES[l] : "?"; }

static const char* validityName(uint8_t v) {
  switch (v) { case 0: return "normal"; case 1: return "warm-up"; case 2: return "start-up"; default: return "invalid"; }
}

// ---------- task 1 Hz ----------
void envTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(300));            // AHT21 butuh >100 ms setelah power-on

  bool ahtOk = ahtInit();
  LOG("[env] AHT21 di 0x%02X %s", AHT_ADDR, ahtOk ? "OK" : "TIDAK TERDETEKSI");
  bool ensOk = ensInit();
  if (!ensOk) LOG("[env] ENS160 TIDAK TERDETEKSI (0x53/0x52)");

  uint32_t lastRetry = millis();
  uint8_t  ahtFail = 0, ensFail = 0, lastValidity = 255;
  TickType_t last = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    uint32_t now = millis();

    // re-init sensor yang hilang tiap 10 s
    if ((!ahtOk || !ensOk) && now - lastRetry > 10000) {
      lastRetry = now;
      if (!ahtOk && (ahtOk = ahtInit()))  LOG("[env] AHT21 terdeteksi");
      if (!ensOk && (ensOk = ensInit()))  LOG("[env] ENS160 terdeteksi");
    }

    // --- AHT21 ---
    float t = NAN, rh = NAN;
    if (ahtOk) {
      bool ok = ahtTrigger();
      vTaskDelay(pdMS_TO_TICKS(85));
      if (ok) ok = ahtRead(t, rh);
      if (ok) {
        ahtFail = 0;
        Lock l;
        g_env.t = t; g_env.rh = rh; g_env.ahtMs = now; g_env.ahtOk = true;
      } else if (++ahtFail >= 5) {
        ahtOk = false; ahtFail = 0;
        { Lock l; g_env.ahtOk = false; }
        LOG("[env] AHT21 gagal dibaca 5x, ditandai OFFLINE");
      }
    }

    // --- ENS160 ---
    if (ensOk) {
      if (!isnan(t)) ensCompensate(t, rh);
      uint8_t v = 3, aqi = 0; uint16_t tvoc = 0, eco2 = 0;
      int r = ensRead(v, aqi, tvoc, eco2);
      if (r < 0) {
        if (++ensFail >= 5) {
          ensOk = false; ensFail = 0;
          { Lock l; g_env.ensOk = false; }
          LOG("[env] ENS160 error/gagal dibaca 5x, ditandai OFFLINE");
        }
      } else {
        ensFail = 0;
        if (v != lastValidity) { lastValidity = v; LOG("[env] ENS160 status: %s", validityName(v)); }
        Lock l;
        g_env.ensOk = true;
        g_env.ensValidity = v;
        if (r == 1) {
          g_env.aqi = aqi; g_env.tvoc = tvoc; g_env.eco2 = eco2; g_env.ensMs = now;
        }
      }
    }

#if LOG_VERBOSE
    EnvData e; { Lock l; e = g_env; }
    LOGV("[env] T=%.1fC RH=%.0f%% eCO2=%u TVOC=%u AQI=%u (%s)", e.t, e.rh, e.eco2, e.tvoc, e.aqi,
         validityName(e.ensValidity));
#endif
  }
}
