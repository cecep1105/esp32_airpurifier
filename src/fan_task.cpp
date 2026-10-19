#include "app.h"
#include "log.h"
#include <esp_arduino_version.h>

// ---------- kategori PM2.5 ----------
static const char* CAT_NAMES[4] = {"AMAN", "CENDERUNG", "BAHAYA", "SGT BAHAYA"};

const char* categoryName(uint8_t c) { return c < 4 ? CAT_NAMES[c] : "?"; }

const char* modeName(uint8_t m) {
  switch (m) {
    case FAN_AUTO:  return "AUTO";
    case FAN_POT:   return "POT";
    case FAN_FIXED: return "FIXED";
  }
  return "?";
}

static uint8_t rawCategory(const Settings& s, float pm) {
  uint8_t c = 0;
  while (c < 3 && pm >= s.th[c]) c++;
  return c;
}

// naik langsung, turun hanya jika pm < batas - histeresis
uint8_t pmCategory(const Settings& s, float pm, uint8_t cur) {
  if (cur > 3) cur = 3;
  uint8_t raw = rawCategory(s, pm);
  if (raw >= cur) return raw;
  while (cur > 0 && pm < s.th[cur - 1] - s.hyst) cur--;
  return cur;
}

// ---------- PWM (kompatibel core Arduino 2.x & 3.x) ----------
static void pwmInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_FAN_PWM, FAN_PWM_FREQ, FAN_PWM_RES);
#else
  ledcSetup(FAN_PWM_CH, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(PIN_FAN_PWM, FAN_PWM_CH);
#endif
}

static void pwmWritePct(float pct) {
  const uint32_t maxd = (1UL << FAN_PWM_RES) - 1;
  pct = constrain(pct, 0.0f, 100.0f);
  uint32_t d = (uint32_t)lroundf(pct * maxd / 100.0f);
#if FAN_PWM_INVERT
  d = maxd - d;
#endif
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_FAN_PWM, d);
#else
  ledcWrite(FAN_PWM_CH, d);
#endif
}

// ---------- tachometer (ISR) ----------
static volatile uint32_t s_tachPulses = 0;
static volatile uint32_t s_tachLastUs = 0;

static void IRAM_ATTR tachIsr() {
  uint32_t t = micros();
  if (t - s_tachLastUs >= FAN_TACH_MIN_US) {
    s_tachLastUs = t;
    s_tachPulses++;
  }
}

// ---------- RGB onboard (WS2812) ----------
static void rgbWrite(uint8_t r, uint8_t g, uint8_t b) {
  r = (uint16_t)r * RGB_BRIGHTNESS / 255;
  g = (uint16_t)g * RGB_BRIGHTNESS / 255;
  b = (uint16_t)b * RGB_BRIGHTNESS / 255;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  rgbLedWrite(PIN_RGB, r, g, b);
#else
  neopixelWrite(PIN_RGB, r, g, b);
#endif
}

// Prioritas indikator:
//  1. kipas macet           : merah <-> biru, kedip cepat
//  2. sensor PM error/belum : ungu kedip 1 Hz
//  3. level terburuk dari kategori PM2.5 dan level eCO2:
//     0 hijau, 1 kuning, 2 oranye, 3 merah kedip 2 Hz
static void rgbUpdate(uint32_t now, bool pmFresh, uint8_t pmCat, bool co2Valid, uint8_t co2Lvl,
                      bool stalled) {
  static uint32_t lastKey = 0xFFFFFFFF;
  uint8_t r = 0, g = 0, b = 0;
  if (stalled) {
    if ((now / 150) & 1) r = 255; else b = 255;
  } else if (!pmFresh) {
    if ((now / 500) & 1) { r = 160; b = 255; }
  } else {
    uint8_t lvl = pmCat;
    if (co2Valid && co2Lvl > lvl) lvl = co2Lvl;
    switch (lvl) {
      case 0:  g = 255; break;
      case 1:  r = 255; g = 180; break;
      case 2:  r = 255; g = 70; break;
      default: if ((now / 250) & 1) r = 255; break;
    }
  }
  uint32_t key = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (key != lastKey) { lastKey = key; rgbWrite(r, g, b); }   // tulis hanya bila berubah
}

// ---------- relay ----------
static void relayWrite(bool on) {
#if FAN_USE_RELAY
  digitalWrite(PIN_FAN_RELAY, (on == (bool)RELAY_ACTIVE_HIGH) ? HIGH : LOW);
#else
  (void)on;
#endif
}

// ---------- task 10 Hz ----------
void fanTask(void*) {
  pwmInit();
  pwmWritePct(0);
  analogReadResolution(12);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  rgbWrite(0, 0, 0);
  LOG("[fan] RGB onboard GPIO%d, brightness %d", PIN_RGB, RGB_BRIGHTNESS);
#if FAN_USE_RELAY
  pinMode(PIN_FAN_RELAY, OUTPUT);
  relayWrite(false);
  LOG("[fan] relay GPIO%d (%s), OFF delay %d ms", PIN_FAN_RELAY, RELAY_ACTIVE_HIGH ? "active HIGH" : "active LOW", FAN_RELAY_OFF_MS);
#endif
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), tachIsr, FALLING);
  LOG("[fan] tach GPIO%d, %d pulsa/putaran", PIN_FAN_TACH, FAN_TACH_PPR);
  LOG("[fan] PWM GPIO%d %d Hz %d bit, core Arduino %d.%d.%d", PIN_FAN_PWM, FAN_PWM_FREQ, FAN_PWM_RES,
      ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);

  const TickType_t period = pdMS_TO_TICKS(100);
  const float dt = 0.1f;
  TickType_t last = xTaskGetTickCount();

  float    out = 0, potFilt = -1;
  uint8_t  cat = 0;
  bool     wasOff = true;
  uint32_t kickUntil = 0;
  bool     btnLast = true;
  uint32_t btnT = 0;
  bool     relayOn = !FAN_USE_RELAY;   // tanpa relay: daya selalu ada
  uint32_t zeroSince = 0;
  uint16_t rpm = 0;
  uint32_t rpmT = millis(), rpmPulses = 0;
  uint32_t stallSince = 0;
  bool     stalled = false;

  for (;;) {
    vTaskDelayUntil(&last, period);
    uint32_t now = millis();

    Settings s; SensorData sd; EnvData env;
    { Lock l; s = g_cfg; sd = g_sensor; env = g_env; }
    bool fresh = sensorFresh(sd, s, now);
    if (fresh) cat = pmCategory(s, sd.pm25Avg, cat);

    // potensio: rata-rata 8 sampel + IIR
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) acc += analogRead(PIN_POT);
    float raw = acc / 8.0f;
    potFilt = (potFilt < 0) ? raw : potFilt + 0.2f * (raw - potFilt);

    float target;
    switch (s.mode) {
      case FAN_POT:
        target = potFilt * 100.0f / 4095.0f;
        if (target < s.potDeadband) target = 0;
        break;
      case FAN_FIXED:
        target = s.fixedDuty;
        break;
      default:
        target = fresh ? s.duty[cat] : s.faultDuty;
        break;
    }

    // slew rate limiter
    if (s.slewPctPerS == 0) {
      out = target;
    } else {
      float step = s.slewPctPerS * dt;
      if (out < target) out = min(out + step, target);
      else              out = max(out - step, target);
    }

    // kick-start kipas dari kondisi mati
    if (out > 0 && wasOff && FAN_KICK_MS > 0) kickUntil = now + FAN_KICK_MS;
    wasOff = (out <= 0.0f);

    // relay daya 12V: ON begitu kipas perlu jalan, OFF setelah 0% selama FAN_RELAY_OFF_MS
#if FAN_USE_RELAY
    if (out > 0.0f) {
      zeroSince = 0;
      if (!relayOn) { relayOn = true; relayWrite(true); LOG("[fan] relay ON"); }
    } else if (relayOn) {
      if (!zeroSince) zeroSince = now;
      if (now - zeroSince >= FAN_RELAY_OFF_MS) { relayOn = false; relayWrite(false); LOG("[fan] relay OFF"); }
    }
#endif
    pwmWritePct(!relayOn ? 0.0f : ((int32_t)(kickUntil - now) > 0 ? 100.0f : out));

    // rpm tiap >= 1 s
    if (now - rpmT >= 1000) {
      uint32_t p = s_tachPulses;
      uint32_t dp = p - rpmPulses;
      uint32_t r = dp * 60000UL / (FAN_TACH_PPR * (now - rpmT));
      rpm = (uint16_t)(r > 65535 ? 65535 : r);
      rpmPulses = p;
      rpmT = now;
      LOGV("[fan] rpm=%u duty=%.0f%% relay=%s", rpm, out, relayOn ? "ON" : "OFF");
    }

    // deteksi macet: daya ada, duty cukup, tapi tach 0 selama 3 s
    if (relayOn && out >= FAN_STALL_MIN_PCT && rpm == 0) {
      if (!stallSince) stallSince = now;
      if (!stalled && now - stallSince >= 3000) {
        stalled = true;
        LOG("[fan] PERINGATAN: rpm 0 pada duty %.0f%% (kipas macet / kabel TACH / pull-up?)", out);
      }
    } else {
      stallSince = 0;
      if (stalled && rpm > 0) { stalled = false; LOG("[fan] tach normal lagi, %u rpm", rpm); }
    }
    static uint8_t lastCat = 255;
    if (fresh && cat != lastCat) {
      LOG("[fan] kategori -> %s (avg PM2.5 %.1f), target %.0f%%", categoryName(cat), sd.pm25Avg, target);
      lastCat = cat;
    }

    // indikator RGB onboard
    bool co2Ok = envCo2Valid(env, now);
    rgbUpdate(now, fresh, cat, co2Ok, co2Ok ? co2Level(env.eco2) : 0, stalled);

    // tombol BOOT: ganti mode (debounce 50 ms)
    bool b = digitalRead(PIN_BTN_MODE);
    if (b != btnLast && now - btnT > 50) {
      btnT = now;
      btnLast = b;
      if (!b) {
        Settings copy;
        { Lock l; g_cfg.mode = (g_cfg.mode + 1) % FAN_MODE_COUNT; copy = g_cfg; }
        bool saved = settingsSave(copy);
        LOG("[fan] tombol: mode -> %s (%s)", modeName(copy.mode), saved ? "tersimpan" : "gagal simpan");
      }
    }

    Lock l;
    g_fan.targetPct = target;
    g_fan.outPct    = out;
    g_fan.category  = cat;
    g_fan.potRaw    = (uint16_t)potFilt;
    g_fan.rpm       = rpm;
    g_fan.relayOn   = relayOn;
    g_fan.stalled   = stalled;
  }
}
