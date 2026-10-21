#pragma once
#include <Arduino.h>
#include "config.h"
#include "settings.h"

// ---------- state bersama antar task ----------
struct SensorData {
  float    pm25     = 0;
  float    pm10     = 0;
  float    pm25Avg  = 0;
  uint32_t lastMs   = 0;
  uint32_t frames   = 0;
  uint32_t errors   = 0;     // checksum / frame rusak
  uint16_t devId    = 0;
  bool     everValid = false;
  bool     queryMode = false;
  bool     sleeping  = false;
};

struct FanState {
  float    targetPct = 0;
  float    outPct    = 0;
  uint8_t  category  = 0;
  uint16_t potRaw    = 0;
  uint16_t rpm       = 0;
  bool     relayOn   = false;
  bool     stalled   = false;
};

enum : uint8_t { CO2_SRC_NONE = 0, CO2_SRC_ENS160 = 1, CO2_SRC_SCD40 = 2 };
enum : uint8_t { TH_SRC_NONE = 0, TH_SRC_AHT21 = 1, TH_SRC_SCD40 = 2 };

struct EnvData {
  bool     ahtOk       = false;
  bool     ensOk       = false;
  bool     scdOk       = false;
  float    t           = NAN;    // C   (AHT21, cadangan SCD40)
  float    rh          = NAN;    // %   (AHT21, cadangan SCD40)
  uint16_t eco2        = 0;      // ppm CO2: SCD40 (NDIR) bila ada, cadangan eCO2 ENS160
  uint16_t tvoc        = 0;      // ppb (ENS160)
  uint8_t  aqi         = 0;      // AQI-UBA 1..5 (ENS160)
  uint8_t  ensValidity = 3;      // 0 normal, 1 warm-up, 2 start-up, 3 invalid
  uint8_t  co2Src      = CO2_SRC_NONE;
  uint8_t  thSrc       = TH_SRC_NONE;
  uint32_t ahtMs       = 0;
  uint32_t ensMs       = 0;
  uint32_t co2Ms       = 0;      // update terakhir eco2 (sumber apa pun)
  uint32_t thMs        = 0;      // update terakhir t/rh (sumber apa pun)
  uint32_t scdMs       = 0;
  float    scdT        = NAN;    // nilai mentah SCD40 (info)
  float    scdRh       = NAN;
};

// PM2.5 luar ruangan (ZH03B)
struct OutdoorData {
  float    pm1       = 0;
  float    pm25      = 0;
  float    pm10      = 0;
  float    pm25Avg   = 0;
  uint32_t lastMs    = 0;
  uint32_t frames    = 0;
  uint32_t errors    = 0;
  bool     everValid = false;
};

enum SdsReqType : uint8_t { SDS_REQ_APPLY_MODE, SDS_REQ_SLEEP, SDS_REQ_WAKE, SDS_REQ_QUERY };
struct SdsReq { SdsReqType type; };

extern SemaphoreHandle_t g_mtx;
extern Settings          g_cfg;
extern SensorData        g_sensor;
extern FanState          g_fan;
extern QueueHandle_t     g_sdsQ;
extern volatile bool     g_displayOk;
extern EnvData           g_env;
extern OutdoorData       g_out;
extern SemaphoreHandle_t g_i2cMtx;
extern volatile bool     g_bleConnected;

// RAII lock untuk g_cfg / g_sensor / g_fan / g_env
struct Lock {
  Lock()  { xSemaphoreTake(g_mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mtx); }
};

// RAII lock bus I2C (OLED + ENS160 + AHT21)
struct I2CLock {
  I2CLock()  { xSemaphoreTake(g_i2cMtx, portMAX_DELAY); }
  ~I2CLock() { xSemaphoreGive(g_i2cMtx); }
};

inline bool sensorFresh(const SensorData& d, const Settings& s, uint32_t now) {
  if (!d.everValid || d.sleeping) return false;
  uint32_t to = SENSOR_TIMEOUT_MS;
  if (s.sdsQueryMode) {
    uint32_t q = (uint32_t)s.sdsQueryIntervalS * 2000UL + 1000UL;
    if (q > to) to = q;
  }
  return (now - d.lastMs) < to;
}

// suhu/RH segar (AHT21 tiap 1 s, SCD40 tiap 5 s)
inline bool envThFresh(const EnvData& e, uint32_t now) { return e.thSrc && e.thMs && (now - e.thMs) < 12000; }
// ada nilai CO2 terbaru (sumber apa pun)
inline bool envCo2Fresh(const EnvData& e, uint32_t now) { return e.co2Src && e.co2Ms && (now - e.co2Ms) < 15000; }
// ENS160 masih pemanasan -> nilai eCO2 belum layak
inline bool envCo2Warm(const EnvData& e) { return e.co2Src == CO2_SRC_ENS160 && e.ensValidity == 1; }
// CO2 dipakai untuk peringatan: SCD40 selalu, ENS160 hanya setelah warm-up
inline bool envCo2Valid(const EnvData& e, uint32_t now) {
  if (!envCo2Fresh(e, now)) return false;
  if (e.co2Src == CO2_SRC_SCD40) return true;
  return e.ensValidity == 0 || e.ensValidity == 2;
}
inline const char* co2SrcName(uint8_t s) { return s == CO2_SRC_SCD40 ? "SCD40" : s == CO2_SRC_ENS160 ? "ENS160" : "-"; }

// ZH03B kirim data tiap 1 s
inline bool outFresh(const OutdoorData& o, uint32_t now) { return o.everValid && (now - o.lastMs) < OUT_TIMEOUT_MS; }

uint8_t     co2Level(uint16_t ppm);        // 0 BAIK .. 3 SGT BURUK
const char* co2LevelName(uint8_t l);

uint8_t     pmCategory(const Settings& s, float pm, uint8_t cur);
const char* categoryName(uint8_t c);
const char* modeName(uint8_t m);

// ---------- WiFi (web_task.cpp) ----------
struct WifiInfo {
  bool staConnected;
  char ssid[33];
  char ip[16];
  int  rssi;
  bool apOn;
  char apSsid[24];
  char apIp[16];
  int  apClients;
};
WifiInfo wifiInfo();
void     wifiSetCredentials(const char* ssid, const char* pass);   // "" = lupakan

void sensorTask(void*);
void fanTask(void*);
void displayTask(void*);
void consoleTask(void*);
void remotexyTask(void*);
void envTask(void*);
void scd40Task(void*);
void zh03bTask(void*);
void webTask(void*);
