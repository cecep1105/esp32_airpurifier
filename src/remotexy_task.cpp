#include "app.h"
#include "log.h"

#if ENABLE_REMOTEXY   // seluruh file (dan stack BLE) hanya ikut bila RemoteXY aktif
#include "remotexy_gui.h"

// Pemetaan GUI:
//  switch_fanmode  ON = AUTO, OFF = MANUAL (mode FIXED, kecepatan dari +/-)
//  button +/-      ubah fixedDuty sebesar RXY_FAN_STEP (hanya di MANUAL)
//  circularBar_01  PM2.5 rata-rata, 100% = RXY_BAR_FULL_PM ug/m3
//  value_status    kategori / status sensor
//  value_fanspeed  duty kipas (%), value_fanrpm rpm dari tach
//  value_temp/rh   AHT21, value_CO2 eCO2 ENS160 (0 = belum ada / offline)

static void setStatus(const char* s) {
  strncpy(RemoteXY.value_status, s, sizeof(RemoteXY.value_status) - 1);
  RemoteXY.value_status[sizeof(RemoteXY.value_status) - 1] = 0;
}

void remotexyTask(void*) {
  RemoteXY_Init();
  LOG("[rxy] BLE aktif, nama \"%s\"", RXY_BLE_NAME);

  uint8_t lastMode;
  { Lock l; lastMode = g_cfg.mode; }
  RemoteXY.switch_fanmode = (lastMode == FAN_AUTO) ? 1 : 0;
  uint8_t  lastSwitch = RemoteXY.switch_fanmode;
  uint8_t  lastAdd = 0, lastSub = 0, lastConn = 0;
  uint32_t dirtyAt = 0;

  for (;;) {
    RemoteXYEngine.handler();
    uint32_t now = millis();

    Settings s; SensorData sd; FanState fs; EnvData env;
    { Lock l; s = g_cfg; sd = g_sensor; fs = g_fan; env = g_env; }

    // --- koneksi ---
    if (RemoteXY.connect_flag != lastConn) {
      lastConn = RemoteXY.connect_flag;
      g_bleConnected = lastConn;
      LOG("[rxy] HP %s", lastConn ? "TERHUBUNG" : "terputus");
    }

    // --- switch mode dari app ---
    if (RemoteXY.switch_fanmode != lastSwitch) {
      lastSwitch = RemoteXY.switch_fanmode;
      uint8_t m = lastSwitch ? FAN_AUTO : FAN_FIXED;
      {
        Lock l;
        g_cfg.mode = m;
        if (m == FAN_FIXED) g_cfg.fixedDuty = (uint8_t)constrain((int)lroundf(fs.outPct), 0, 100);  // lanjut dari kecepatan sekarang
      }
      s.mode = m;
      lastMode = m;
      dirtyAt = now;
      LOG("[rxy] app: mode -> %s", lastSwitch ? "AUTO" : "MANUAL (FIXED)");
    } else if (s.mode != lastMode) {
      // mode diubah dari tombol BOOT / console -> sinkronkan switch di app
      lastMode = s.mode;
      RemoteXY.switch_fanmode = (s.mode == FAN_AUTO) ? 1 : 0;
      lastSwitch = RemoteXY.switch_fanmode;
    }

    // --- tombol +/- (deteksi tepi tekan) ---
    int step = 0;
    if (RemoteXY.button_fanadd && !lastAdd) step = +RXY_FAN_STEP;
    if (RemoteXY.button_fansub && !lastSub) step = -RXY_FAN_STEP;
    lastAdd = RemoteXY.button_fanadd;
    lastSub = RemoteXY.button_fansub;
    if (step) {
      if (s.mode == FAN_FIXED) {
        uint8_t d = (uint8_t)constrain((int)s.fixedDuty + step, 0, 100);
        { Lock l; g_cfg.fixedDuty = d; }
        dirtyAt = now;
        LOG("[rxy] app: kecepatan manual -> %u%%", d);
      } else {
        LOG("[rxy] app: +/- diabaikan, mode %s (pindah ke MANUAL dulu)", modeName(s.mode));
      }
    }

    // --- simpan ke NVS 3 s setelah perubahan terakhir ---
    if (dirtyAt && now - dirtyAt > 3000) {
      Settings c;
      { Lock l; c = g_cfg; }
      bool ok = settingsSave(c);
      LOG("[rxy] setting %s", ok ? "tersimpan" : "GAGAL disimpan");
      dirtyAt = 0;
    }

    // --- output ke app ---
    bool fresh = sensorFresh(sd, s, now);
    RemoteXY.value_pm25     = fresh ? sd.pm25Avg : 0.0f;
    RemoteXY.value_pm10     = fresh ? sd.pm10    : 0.0f;
    RemoteXY.circularBar_01 = fresh ? (int8_t)constrain((int)lroundf(sd.pm25Avg * 100.0f / RXY_BAR_FULL_PM), 0, 100) : 0;
    if (sd.sleeping)        setStatus("SLEEP");
    else if (!sd.everValid) setStatus("MENUNGGU");
    else if (!fresh)        setStatus("NO SENSOR");
    else                    setStatus(categoryName(fs.category));
    RemoteXY.value_fanspeed = (int8_t)constrain((int)lroundf(fs.outPct), 0, 100);
    RemoteXY.value_fanrpm   = (int16_t)(fs.rpm > 32767 ? 32767 : fs.rpm);
    RemoteXY.value_temp     = envThFresh(env, now) ? env.t : 0.0f;
    RemoteXY.value_rh       = envThFresh(env, now) ? (int8_t)lroundf(env.rh) : 0;
    RemoteXY.value_CO2      = (env.ensOk && env.ensMs && now - env.ensMs < 10000) ? (int16_t)(env.eco2 > 32767 ? 32767 : env.eco2) : 0;

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#endif  // ENABLE_REMOTEXY
