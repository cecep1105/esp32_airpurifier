#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "settings.h"

static const uint8_t SETTINGS_VERSION = 1;
static const char* NVS_NS  = "cair";
static const char* NVS_KEY = "cfg";

void settingsDefaults(Settings& s) {
  s.version           = SETTINGS_VERSION;
  s.mode              = DEF_MODE;
  s.th[0]             = DEF_TH1;
  s.th[1]             = DEF_TH2;
  s.th[2]             = DEF_TH3;
  s.duty[0]           = DEF_DUTY0;
  s.duty[1]           = DEF_DUTY1;
  s.duty[2]           = DEF_DUTY2;
  s.duty[3]           = DEF_DUTY3;
  s.fixedDuty         = DEF_FIXED_DUTY;
  s.faultDuty         = DEF_FAULT_DUTY;
  s.hyst              = DEF_HYST;
  s.slewPctPerS       = DEF_SLEW;
  s.avgWindow         = DEF_AVG;
  s.sdsQueryMode      = DEF_SDS_QUERY;
  s.sdsQueryIntervalS = DEF_SDS_QINT_S;
  s.potDeadband       = DEF_POT_DEADBAND;
}

static uint8_t clampU8(int v, int lo, int hi) { return (uint8_t)constrain(v, lo, hi); }

void settingsValidate(Settings& s) {
  if (s.mode >= FAN_MODE_COUNT) s.mode = FAN_AUTO;
  for (int i = 0; i < 3; i++) s.th[i] = constrain(s.th[i], 0.0f, 999.0f);
  if (!(s.th[0] < s.th[1] && s.th[1] < s.th[2])) { s.th[0] = DEF_TH1; s.th[1] = DEF_TH2; s.th[2] = DEF_TH3; }
  for (int i = 0; i < 4; i++) s.duty[i] = clampU8(s.duty[i], 0, 100);
  s.fixedDuty         = clampU8(s.fixedDuty, 0, 100);
  s.faultDuty         = clampU8(s.faultDuty, 0, 100);
  s.hyst              = constrain(s.hyst, 0.0f, 10.0f);
  s.slewPctPerS       = clampU8(s.slewPctPerS, 0, 100);
  s.avgWindow         = clampU8(s.avgWindow, 1, AVG_MAX);
  s.sdsQueryMode      = s.sdsQueryMode ? 1 : 0;
  s.sdsQueryIntervalS = (uint16_t)constrain((int)s.sdsQueryIntervalS, 2, 600);
  s.potDeadband       = clampU8(s.potDeadband, 0, 20);
}

bool settingsLoad(Settings& s) {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  size_t n = p.getBytes(NVS_KEY, &s, sizeof(s));
  p.end();
  if (n != sizeof(s) || s.version != SETTINGS_VERSION) return false;
  settingsValidate(s);
  return true;
}

bool settingsSave(const Settings& s) {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  size_t n = p.putBytes(NVS_KEY, &s, sizeof(s));
  p.end();
  return n == sizeof(s);
}
