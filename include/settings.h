#pragma once
#include <stdint.h>

enum FanMode : uint8_t { FAN_AUTO = 0, FAN_POT = 1, FAN_FIXED = 2, FAN_MODE_COUNT };

struct Settings {
  uint8_t  version;
  uint8_t  mode;               // FanMode
  float    th[3];              // batas kategori PM2.5 (ug/m3), naik
  uint8_t  duty[4];            // % kipas per kategori (AUTO)
  uint8_t  fixedDuty;          // % kipas mode FIXED
  uint8_t  faultDuty;          // % kipas saat AUTO & sensor fault
  float    hyst;               // histeresis turun kategori (ug/m3)
  uint8_t  slewPctPerS;        // laju perubahan kipas (%/s), 0 = langsung
  uint8_t  avgWindow;          // jumlah sampel moving average
  uint8_t  sdsQueryMode;       // 0 active, 1 query
  uint16_t sdsQueryIntervalS;  // interval query (s)
  uint8_t  potDeadband;        // % potensio dianggap 0
};

void settingsDefaults(Settings& s);
void settingsValidate(Settings& s);
bool settingsLoad(Settings& s);
bool settingsSave(const Settings& s);
