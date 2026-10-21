#include "app.h"

// Console di UART0 (LOG_SERIAL). Perintah diakhiri Enter.
#define OUT LOG_SERIAL

static bool s_log = true;
static uint16_t s_logIntervalMs = 2000;

static void printHelp() {
  OUT.println(F(
    "\n--- CAIR console ---\n"
    "help                      daftar perintah\n"
    "status                    status sensor & kipas\n"
    "cfg                       tampilkan konfigurasi\n"
    "mode auto|pot|fixed       mode kipas\n"
    "fixed <0-100>             duty mode FIXED (%)\n"
    "fault <0-100>             duty AUTO saat sensor error (%)\n"
    "th <t1> <t2> <t3>         batas kategori PM2.5 (ug/m3)\n"
    "duty <d0> <d1> <d2> <d3>  duty per kategori AUTO (%)\n"
    "hyst <ug/m3>              histeresis turun kategori\n"
    "slew <%/s>                laju perubahan kipas (0=langsung)\n"
    "avg <1-30>                jumlah sampel moving average\n"
    "dead <0-20>               deadband potensio (%)\n"
    "sds active                SDS011 kirim data tiap 1 s\n"
    "sds query [detik]         SDS011 query mode\n"
    "sds sleep|wake|read       kontrol sensor\n"
    "env                       suhu, RH, CO2, TVOC, AQI, PM luar\n"
    "wifi                      status WiFi\n"
    "wifi <ssid> [password]    sambung ke router (disimpan)\n"
    "wifi forget               hapus SSID tersimpan\n"
    "log on|off [ms]           log status periodik\n"
    "save | load | defaults    NVS\n"));
}

static void printStatus() {
  Settings s; SensorData d; FanState f;
  { Lock l; s = g_cfg; d = g_sensor; f = g_fan; }
  bool fresh = sensorFresh(d, s, millis());
  if (fresh) {
    OUT.printf("[st] PM2.5=%.1f avg=%.1f PM10=%.1f cat=%s",
               d.pm25, d.pm25Avg, d.pm10, categoryName(f.category));
  } else {
    OUT.printf("[st] sensor=%s", d.sleeping ? "SLEEP" : (d.everValid ? "TIMEOUT" : "WAIT"));
  }
  EnvData e; { Lock l; e = g_env; }
  uint32_t now = millis();
  OutdoorData o; { Lock l; o = g_out; }
  if (outFresh(o, now)) {
    OUT.printf(" | luar=%.1f", o.pm25Avg);
    if (sensorFresh(d, s, now)) OUT.printf(" d=%+.1f", d.pm25Avg - o.pm25Avg);
  }
  if (envThFresh(e, now)) OUT.printf(" | T=%.1f RH=%.0f", e.t, e.rh);
  if (envCo2Fresh(e, now)) OUT.printf(" CO2=%u(%s)%s", e.eco2, co2SrcName(e.co2Src), envCo2Warm(e) ? "warm" : "");
  OUT.printf(" | mode=%s fan=%.0f/%.0f%% rpm=%u rly=%s%s pot=%u | frm=%lu err=%lu%s\n",
             modeName(s.mode), f.outPct, f.targetPct, f.rpm, f.relayOn ? "ON" : "OFF",
             f.stalled ? " MACET" : "", f.potRaw,
             (unsigned long)d.frames, (unsigned long)d.errors,
             g_displayOk ? "" : " oled=FAIL");
}

static void printCfg() {
  Settings s;
  { Lock l; s = g_cfg; }
  OUT.printf("mode=%s fixed=%u%% fault=%u%%\n", modeName(s.mode), s.fixedDuty, s.faultDuty);
  OUT.printf("th = %.1f / %.1f / %.1f ug/m3, hyst=%.1f\n", s.th[0], s.th[1], s.th[2], s.hyst);
  OUT.printf("duty AMAN=%u CENDERUNG=%u BAHAYA=%u SGT=%u (%%)\n", s.duty[0], s.duty[1], s.duty[2], s.duty[3]);
  OUT.printf("slew=%u%%/s avg=%u dead=%u%%\n", s.slewPctPerS, s.avgWindow, s.potDeadband);
  OUT.printf("sds=%s interval=%us\n", s.sdsQueryMode ? "query" : "active", s.sdsQueryIntervalS);
}

static void sdsRequest(SdsReqType t) {
  SdsReq r{t};
  if (xQueueSend(g_sdsQ, &r, pdMS_TO_TICKS(100)) != pdTRUE) OUT.println("ERR antrian sensor penuh");
}

static int argc_of(char** argv, int max) { int n = 0; while (n < max && argv[n]) n++; return n; }

static void handleLine(char* line) {
  char* argv[6] = {nullptr};
  int i = 0;
  for (char* t = strtok(line, " \t"); t && i < 6; t = strtok(nullptr, " \t")) argv[i++] = t;
  int argc = argc_of(argv, 6);
  if (argc == 0) return;
  for (char* p = argv[0]; *p; p++) *p = tolower(*p);
  const char* c = argv[0];

  if (!strcmp(c, "help") || !strcmp(c, "?")) { printHelp(); return; }
  if (!strcmp(c, "status")) { printStatus(); return; }
  if (!strcmp(c, "cfg"))    { printCfg(); return; }

  if (!strcmp(c, "env")) {
    EnvData e; { Lock l; e = g_env; }
    static const char* V[4] = {"normal", "warm-up (3 mnt)", "start-up (1 jam pertama)", "invalid"};
    OUT.printf("AHT21 : %s  T=%.1f C  RH=%.1f %%\n", e.ahtOk ? "OK" : "OFFLINE", e.t, e.rh);
    OUT.printf("ENS160: %s  TVOC=%u ppb  AQI=%u  status=%s\n",
               e.ensOk ? "OK" : "OFFLINE", e.tvoc, e.aqi, V[e.ensValidity & 3]);
    OUT.printf("SCD40 : %s  T=%.1f C  RH=%.0f %%\n", e.scdOk ? "OK" : "OFFLINE", e.scdT, e.scdRh);
    OUT.printf("CO2   : %u ppm (%s) dari %s, suhu/RH dari %s\n", e.eco2, co2LevelName(co2Level(e.eco2)),
               co2SrcName(e.co2Src), e.thSrc == TH_SRC_AHT21 ? "AHT21" : e.thSrc == TH_SRC_SCD40 ? "SCD40" : "-");
    OutdoorData o; { Lock l; o = g_out; }
    OUT.printf("ZH03B : %s  PM1=%.0f PM2.5=%.0f (avg %.1f) PM10=%.0f  frm=%lu err=%lu\n",
               outFresh(o, millis()) ? "OK" : "OFFLINE", o.pm1, o.pm25, o.pm25Avg, o.pm10,
               (unsigned long)o.frames, (unsigned long)o.errors);
    return;
  }

  if (!strcmp(c, "wifi")) {
    if (argc == 1) {
      WifiInfo w = wifiInfo();
      OUT.printf("STA: %s ssid=\"%s\" ip=%s rssi=%d\n", w.staConnected ? "TERHUBUNG" : "tidak",
                 w.ssid, w.ip, w.rssi);
      OUT.printf("AP : %s ssid=\"%s\" ip=%s klien=%d\n", w.apOn ? "aktif" : "mati", w.apSsid, w.apIp, w.apClients);
      OUT.printf("web: http://%s/  atau  http://%s.local/\n", w.staConnected ? w.ip : w.apIp, WIFI_HOSTNAME);
    } else if (!strcmp(argv[1], "forget")) {
      wifiSetCredentials("", "");
      OUT.println("OK SSID dihapus");
    } else {
      // SSID yang mengandung spasi: atur lewat halaman web
      wifiSetCredentials(argv[1], argc >= 3 ? argv[2] : "");
      OUT.printf("OK menyambung ke \"%s\"...\n", argv[1]);
    }
    return;
  }

  if (!strcmp(c, "save")) {
    Settings s; { Lock l; s = g_cfg; }
    OUT.println(settingsSave(s) ? "OK tersimpan" : "ERR simpan NVS");
    return;
  }
  if (!strcmp(c, "load")) {
    Settings s;
    if (settingsLoad(s)) { { Lock l; g_cfg = s; } sdsRequest(SDS_REQ_APPLY_MODE); OUT.println("OK dimuat"); }
    else OUT.println("ERR tidak ada data tersimpan");
    return;
  }
  if (!strcmp(c, "defaults")) {
    Settings s; settingsDefaults(s);
    { Lock l; g_cfg = s; }
    sdsRequest(SDS_REQ_APPLY_MODE);
    OUT.println("OK default (ketik save untuk menyimpan)");
    return;
  }

  if (!strcmp(c, "log") && argc >= 2) {
    s_log = !strcmp(argv[1], "on");
    if (argc >= 3) s_logIntervalMs = constrain(atoi(argv[2]), 200, 60000);
    OUT.printf("OK log %s %ums\n", s_log ? "on" : "off", s_logIntervalMs);
    return;
  }

  if (!strcmp(c, "sds") && argc >= 2) {
    if (!strcmp(argv[1], "active")) {
      { Lock l; g_cfg.sdsQueryMode = 0; }
      sdsRequest(SDS_REQ_APPLY_MODE);
    } else if (!strcmp(argv[1], "query")) {
      { Lock l; g_cfg.sdsQueryMode = 1; if (argc >= 3) g_cfg.sdsQueryIntervalS = atoi(argv[2]); settingsValidate(g_cfg); }
      sdsRequest(SDS_REQ_APPLY_MODE);
    } else if (!strcmp(argv[1], "sleep")) sdsRequest(SDS_REQ_SLEEP);
    else if (!strcmp(argv[1], "wake"))    sdsRequest(SDS_REQ_WAKE);
    else if (!strcmp(argv[1], "read"))    sdsRequest(SDS_REQ_QUERY);
    else { OUT.println("ERR sds active|query|sleep|wake|read"); return; }
    OUT.println("OK");
    return;
  }

  // ----- perintah yang mengubah g_cfg -----
  bool ok = true;
  {
    Lock l;
    Settings& s = g_cfg;
    if (!strcmp(c, "mode") && argc >= 2) {
      if      (!strcmp(argv[1], "auto"))  s.mode = FAN_AUTO;
      else if (!strcmp(argv[1], "pot"))   s.mode = FAN_POT;
      else if (!strcmp(argv[1], "fixed")) s.mode = FAN_FIXED;
      else ok = false;
    } else if (!strcmp(c, "fixed") && argc >= 2) s.fixedDuty   = constrain(atoi(argv[1]), 0, 100);
    else if (!strcmp(c, "fault") && argc >= 2)   s.faultDuty   = constrain(atoi(argv[1]), 0, 100);
    else if (!strcmp(c, "hyst")  && argc >= 2)   s.hyst        = atof(argv[1]);
    else if (!strcmp(c, "slew")  && argc >= 2)   s.slewPctPerS = constrain(atoi(argv[1]), 0, 100);
    else if (!strcmp(c, "avg")   && argc >= 2)   s.avgWindow   = constrain(atoi(argv[1]), 1, AVG_MAX);
    else if (!strcmp(c, "dead")  && argc >= 2)   s.potDeadband = constrain(atoi(argv[1]), 0, 20);
    else if (!strcmp(c, "th") && argc >= 4) {
      float a = atof(argv[1]), b = atof(argv[2]), d = atof(argv[3]);
      if (a < b && b < d) { s.th[0] = a; s.th[1] = b; s.th[2] = d; }
      else ok = false;
    } else if (!strcmp(c, "duty") && argc >= 5) {
      for (int k = 0; k < 4; k++) s.duty[k] = constrain(atoi(argv[k + 1]), 0, 100);
    } else ok = false;
    if (ok) settingsValidate(s);
  }
  OUT.println(ok ? "OK (ketik save untuk menyimpan)" : "ERR perintah/argumen salah, ketik help");
}

void consoleTask(void*) {
  char buf[96];
  size_t n = 0;
  uint32_t lastLog = 0;

  OUT.println("\nCAIR air purifier - " BOARD_NAME);
  printCfg();
  OUT.println("ketik 'help' untuk daftar perintah");

  for (;;) {
    while (OUT.available()) {
      char ch = OUT.read();
      if (ch == '\r' || ch == '\n') {
        if (n) { buf[n] = 0; handleLine(buf); n = 0; }
      } else if (ch == 8 || ch == 127) {
        if (n) n--;
      } else if (n < sizeof(buf) - 1) {
        buf[n++] = ch;
      }
    }
    uint32_t now = millis();
    if (s_log && now - lastLog >= s_logIntervalMs) { lastLog = now; printStatus(); }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
