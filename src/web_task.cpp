// =====================================================================
//  WiFi + web dashboard
//   - AP  : CAIR-XXXX / WIFI_AP_PASS, http://192.168.4.1 (captive portal)
//   - STA : router (SSID disimpan di NVS "wifi"), http://cair.local
//   - REST API JSON untuk halaman web (web/index.html -> include/web_index.h)
//   - riwayat 2 jam (HIST_LEN x HIST_INTERVAL_S) + unduh CSV
// =====================================================================
#include "app.h"
#include "log.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>
#include "web_index.h"

static WebServer server(WEB_PORT);
static DNSServer dns;

// ---------------------------------------------------------------------
// WiFi manager
// ---------------------------------------------------------------------
static char s_ssid[33] = "";
static char s_pass[65] = "";
static char s_apSsid[24] = "";
static bool s_apOn = false;
static bool s_staUp = false;
static bool s_timeSync = false;
static uint32_t s_lastAttempt = 0;

static volatile bool s_credPending = false;
static char s_newSsid[33], s_newPass[65];

static void credsLoad() {
  Preferences p;
  if (!p.begin("wifi", true)) return;
  p.getString("ssid", s_ssid, sizeof(s_ssid));
  p.getString("pass", s_pass, sizeof(s_pass));
  p.end();
}

static void credsSave() {
  Preferences p;
  if (!p.begin("wifi", false)) return;
  p.putString("ssid", s_ssid);
  p.putString("pass", s_pass);
  p.end();
}

void wifiSetCredentials(const char* ssid, const char* pass) {
  Lock l;
  strlcpy(s_newSsid, ssid ? ssid : "", sizeof(s_newSsid));
  strlcpy(s_newPass, pass ? pass : "", sizeof(s_newPass));
  s_credPending = true;
}

WifiInfo wifiInfo() {
  WifiInfo w{};
  w.staConnected = WiFi.status() == WL_CONNECTED;
  strlcpy(w.ssid, s_ssid, sizeof(w.ssid));
  strlcpy(w.ip, w.staConnected ? WiFi.localIP().toString().c_str() : "-", sizeof(w.ip));
  w.rssi = w.staConnected ? WiFi.RSSI() : 0;
  w.apOn = s_apOn;
  strlcpy(w.apSsid, s_apSsid, sizeof(w.apSsid));
  strlcpy(w.apIp, s_apOn ? WiFi.softAPIP().toString().c_str() : "-", sizeof(w.apIp));
  w.apClients = s_apOn ? WiFi.softAPgetStationNum() : 0;
  return w;
}

static void apStart() {
  if (s_apOn) return;
  WiFi.mode(WIFI_AP_STA);                    // STA tetap aktif agar scan WiFi bisa jalan
  WiFi.softAP(s_apSsid, WIFI_AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  s_apOn = true;
  LOG("[wifi] AP \"%s\" aktif, http://%s/", s_apSsid, WiFi.softAPIP().toString().c_str());
}

static void apStop() {
  if (!s_apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  s_apOn = false;
  LOG("[wifi] AP dimatikan (STA sudah terhubung)");
}

static void staConnect() {
  if (!s_ssid[0]) return;
  s_lastAttempt = millis();
  WiFi.disconnect(false, false);
  WiFi.begin(s_ssid, s_pass);
  LOG("[wifi] menyambung ke \"%s\"...", s_ssid);
}

static void wifiBegin() {
  credsLoad();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(s_apSsid, sizeof(s_apSsid), "%s%02X%02X", WIFI_AP_PREFIX, mac[4], mac[5]);

  WiFi.persistent(false);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.mode((WIFI_AP_ALWAYS || !s_ssid[0]) ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(true);                      // wajib saat WiFi + BLE jalan bersamaan
  if (WIFI_AP_ALWAYS || !s_ssid[0]) apStart();
  if (s_ssid[0]) staConnect();
  else LOG("[wifi] belum ada SSID router, atur lewat web (AP) atau console: wifi <ssid> <pass>");

  if (MDNS.begin(WIFI_HOSTNAME)) {
    MDNS.addService("http", "tcp", WEB_PORT);
    LOG("[wifi] mDNS http://%s.local/", WIFI_HOSTNAME);
  }
}

static void wifiTick(uint32_t now) {
  // kredensial baru dari web / console
  if (s_credPending) {
    { Lock l; strlcpy(s_ssid, s_newSsid, sizeof(s_ssid)); strlcpy(s_pass, s_newPass, sizeof(s_pass)); s_credPending = false; }
    credsSave();
    if (s_ssid[0]) {
      staConnect();
    } else {
      WiFi.disconnect(false, false);
      apStart();
      LOG("[wifi] SSID router dihapus");
    }
  }

  bool up = WiFi.status() == WL_CONNECTED;
  if (up != s_staUp) {
    s_staUp = up;
    if (up) {
      LOG("[wifi] TERHUBUNG \"%s\" ip=%s rssi=%d dBm", s_ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
      configTzTime("WIB-7", "pool.ntp.org", "time.google.com");
      if (!WIFI_AP_ALWAYS) apStop();
    } else {
      LOG("[wifi] koneksi \"%s\" terputus", s_ssid);
    }
  }

  // coba ulang tiap 30 s; tanpa AP_ALWAYS, AP dinyalakan lagi agar tetap bisa diakses
  if (s_ssid[0] && !up && now - s_lastAttempt > 30000) {
    if (!WIFI_AP_ALWAYS) apStart();
    staConnect();
  }

  if (!s_timeSync && time(nullptr) > 1700000000) {
    s_timeSync = true;
    struct tm tmv; time_t t = time(nullptr); localtime_r(&t, &tmv);
    LOG("[wifi] waktu NTP %04d-%02d-%02d %02d:%02d WIB", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min);
  }
}

// ---------------------------------------------------------------------
// Riwayat (ring buffer)
// ---------------------------------------------------------------------
struct HistSample {
  uint32_t up;       // detik sejak boot
  uint32_t epoch;    // 0 = waktu belum sinkron
  int16_t  pm25;     // x10, -1 = tidak valid
  int16_t  pm10;     // x10
  int16_t  eco2;     // ppm, -1
  int16_t  temp;     // x10, INT16_MIN = tidak valid
  int8_t   rh;       // %, -1
  uint8_t  fan;      // %
  uint16_t rpm;
};
static HistSample s_hist[HIST_LEN];
static uint16_t   s_histHead = 0, s_histCount = 0;
static uint32_t   s_histLast = 0;

static void historyTick(uint32_t now) {
  if (s_histLast && now - s_histLast < HIST_INTERVAL_S * 1000UL) return;
  s_histLast = now;
  Settings s; SensorData sd; FanState fs; EnvData e;
  { Lock l; s = g_cfg; sd = g_sensor; fs = g_fan; e = g_env; }
  HistSample h;
  h.up    = now / 1000;
  h.epoch = s_timeSync ? (uint32_t)time(nullptr) : 0;
  bool pm = sensorFresh(sd, s, now);
  h.pm25  = pm ? (int16_t)lroundf(sd.pm25Avg * 10) : -1;
  h.pm10  = pm ? (int16_t)lroundf(sd.pm10 * 10) : -1;
  bool co2 = e.ensOk && e.ensMs && (now - e.ensMs) < 10000 && e.ensValidity != 1;
  h.eco2  = co2 ? (int16_t)(e.eco2 > 32767 ? 32767 : e.eco2) : -1;
  bool th = envThFresh(e, now);
  h.temp  = th ? (int16_t)lroundf(e.t * 10) : INT16_MIN;
  h.rh    = th ? (int8_t)lroundf(e.rh) : -1;
  h.fan   = (uint8_t)lroundf(fs.outPct);
  h.rpm   = fs.rpm;
  s_hist[s_histHead] = h;
  s_histHead = (s_histHead + 1) % HIST_LEN;
  if (s_histCount < HIST_LEN) s_histCount++;
}

static const HistSample& histAt(uint16_t i) {          // i = 0 paling lama
  uint16_t start = (s_histHead + HIST_LEN - s_histCount) % HIST_LEN;
  return s_hist[(start + i) % HIST_LEN];
}

// ---------------------------------------------------------------------
// JSON helper
// ---------------------------------------------------------------------
static void jNum(String& o, const char* k, float v, int dec, bool valid = true) {
  o += '"'; o += k; o += "\":";
  if (!valid || isnan(v)) { o += "null"; return; }
  char b[16]; snprintf(b, sizeof(b), "%.*f", dec, v); o += b;
}
static void jInt(String& o, const char* k, long v) { o += '"'; o += k; o += "\":"; o += (long)v; }
static void jBool(String& o, const char* k, bool v) { o += '"'; o += k; o += "\":"; o += v ? "true" : "false"; }
static void jStr(String& o, const char* k, const char* v) {
  o += '"'; o += k; o += "\":\"";
  for (const char* p = v; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += c;
  }
  o += '"';
}

static const char* modeKey(uint8_t m) { return m == FAN_AUTO ? "auto" : m == FAN_POT ? "pot" : "fixed"; }

static void sendJson(const String& s, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", s);
}
static void sendErr(const char* msg, int code = 400) {
  String o = "{"; jStr(o, "error", msg); o += "}";
  sendJson(o, code);
}

// ---------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------
static uint32_t s_dirtyAt = 0;     // simpan setting tertunda (anti aus NVS)

static void hIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", (const char*)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static void hStatus() {
  Settings s; SensorData sd; FanState fs; EnvData e;
  { Lock l; s = g_cfg; sd = g_sensor; fs = g_fan; e = g_env; }
  uint32_t now = millis();
  bool pm = sensorFresh(sd, s, now);
  bool th = envThFresh(e, now);
  bool ensFresh = e.ensOk && e.ensMs && (now - e.ensMs) < 10000;
  WifiInfo w = wifiInfo();

  String o; o.reserve(900);
  o += '{';
  jInt(o, "up", now / 1000); o += ',';
  char tbuf[24] = "";
  if (s_timeSync) {
    time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
  }
  jStr(o, "time", tbuf); o += ',';

  o += "\"pm\":{";
  jBool(o, "ok", pm); o += ',';
  jStr(o, "state", sd.sleeping ? "SLEEP" : pm ? "OK" : sd.everValid ? "TIMEOUT" : "WAIT"); o += ',';
  jNum(o, "pm25", sd.pm25, 1, pm); o += ',';
  jNum(o, "avg", sd.pm25Avg, 1, pm); o += ',';
  jNum(o, "pm10", sd.pm10, 1, pm); o += ',';
  jInt(o, "cat", fs.category); o += "},";

  o += "\"env\":{";
  jBool(o, "aht", e.ahtOk); o += ',';
  jBool(o, "ens", e.ensOk); o += ',';
  jNum(o, "t", e.t, 1, th); o += ',';
  jNum(o, "rh", e.rh, 0, th); o += ',';
  jNum(o, "eco2", e.eco2, 0, ensFresh); o += ',';
  jNum(o, "tvoc", e.tvoc, 0, ensFresh); o += ',';
  jNum(o, "aqi", e.aqi, 0, ensFresh); o += ',';
  jInt(o, "val", e.ensValidity); o += ',';
  bool cv = envCo2Valid(e, now);
  jBool(o, "co2ok", cv); o += ',';
  jInt(o, "co2lvl", cv ? co2Level(e.eco2) : -1); o += "},";

  o += "\"fan\":{";
  jStr(o, "mode", modeKey(s.mode)); o += ',';
  jNum(o, "duty", fs.outPct, 0); o += ',';
  jNum(o, "target", fs.targetPct, 0); o += ',';
  jInt(o, "fixed", s.fixedDuty); o += ',';
  jInt(o, "rpm", fs.rpm); o += ',';
  jBool(o, "relay", fs.relayOn); o += ',';
  jBool(o, "stall", fs.stalled); o += "},";

  char th3[48]; snprintf(th3, sizeof(th3), "\"th\":[%.1f,%.1f,%.1f],", s.th[0], s.th[1], s.th[2]); o += th3;
  snprintf(th3, sizeof(th3), "\"co2th\":[%d,%d,%d],", CO2_TH1, CO2_TH2, CO2_TH3); o += th3;

  o += "\"net\":{";
  jBool(o, "sta", w.staConnected); o += ',';
  jStr(o, "ssid", w.ssid); o += ',';
  jStr(o, "ip", w.ip); o += ',';
  jInt(o, "rssi", w.rssi); o += ',';
  jBool(o, "apOn", w.apOn); o += ',';
  jStr(o, "ap", w.apSsid); o += ',';
  jStr(o, "apIp", w.apIp); o += ',';
  jBool(o, "ble", g_bleConnected); o += "}}";
  sendJson(o);
}

static void hMode() {
  String m = server.arg("mode");
  uint8_t mode;
  if (m == "auto") mode = FAN_AUTO;
  else if (m == "pot") mode = FAN_POT;
  else if (m == "fixed") mode = FAN_FIXED;
  else { sendErr("mode harus auto|fixed|pot"); return; }
  {
    Lock l;
    if (mode == FAN_FIXED && g_cfg.mode != FAN_FIXED)       // lanjut dari kecepatan sekarang
      g_cfg.fixedDuty = (uint8_t)constrain((int)lroundf(g_fan.outPct), 0, 100);
    g_cfg.mode = mode;
  }
  s_dirtyAt = millis();
  LOG("[web] mode -> %s", modeName(mode));
  hStatus();
}

static void hFan() {
  if (!server.hasArg("duty")) { sendErr("parameter duty wajib"); return; }
  int d = constrain(server.arg("duty").toInt(), 0, 100);
  { Lock l; g_cfg.mode = FAN_FIXED; g_cfg.fixedDuty = d; }
  s_dirtyAt = millis();
  LOG("[web] kecepatan manual -> %d%%", d);
  hStatus();
}

static void sendConfig() {
  Settings s; { Lock l; s = g_cfg; }
  String o; o.reserve(300);
  o += '{';
  char b[64];
  snprintf(b, sizeof(b), "\"th\":[%.1f,%.1f,%.1f],", s.th[0], s.th[1], s.th[2]); o += b;
  snprintf(b, sizeof(b), "\"duty\":[%u,%u,%u,%u],", s.duty[0], s.duty[1], s.duty[2], s.duty[3]); o += b;
  jInt(o, "fault", s.faultDuty); o += ',';
  jInt(o, "slew", s.slewPctPerS); o += ',';
  jNum(o, "hyst", s.hyst, 1); o += ',';
  jInt(o, "avg", s.avgWindow); o += ',';
  jInt(o, "dead", s.potDeadband); o += '}';
  sendJson(o);
}

static void hConfigPost() {
  Settings n; { Lock l; n = g_cfg; }
  auto f = [](const char* k, float def) { return server.hasArg(k) ? server.arg(k).toFloat() : def; };
  float t1 = f("th1", n.th[0]), t2 = f("th2", n.th[1]), t3 = f("th3", n.th[2]);
  if (!(t1 > 0 && t1 < t2 && t2 < t3)) { sendErr("batas harus naik: th1 < th2 < th3"); return; }
  n.th[0] = t1; n.th[1] = t2; n.th[2] = t3;
  const char* dk[4] = {"d0", "d1", "d2", "d3"};
  for (int i = 0; i < 4; i++) n.duty[i] = (uint8_t)constrain((int)f(dk[i], n.duty[i]), 0, 100);
  n.faultDuty   = (uint8_t)constrain((int)f("fault", n.faultDuty), 0, 100);
  n.slewPctPerS = (uint8_t)constrain((int)f("slew", n.slewPctPerS), 0, 100);
  n.hyst        = f("hyst", n.hyst);
  n.avgWindow   = (uint8_t)constrain((int)f("avg", n.avgWindow), 1, AVG_MAX);
  n.potDeadband = (uint8_t)constrain((int)f("dead", n.potDeadband), 0, 20);
  settingsValidate(n);
  {
    Lock l;
    n.mode = g_cfg.mode; n.fixedDuty = g_cfg.fixedDuty;   // jangan timpa perubahan dari sumber lain
    g_cfg = n;
  }
  bool ok = settingsSave(n);
  LOG("[web] setting otomatis diubah & %s", ok ? "tersimpan" : "GAGAL disimpan");
  sendConfig();
}

static void hConfigDefaults() {
  Settings d; settingsDefaults(d);
  Settings n;
  { Lock l; d.mode = g_cfg.mode; d.fixedDuty = g_cfg.fixedDuty; d.sdsQueryMode = g_cfg.sdsQueryMode;
    d.sdsQueryIntervalS = g_cfg.sdsQueryIntervalS; g_cfg = d; n = d; }
  settingsSave(n);
  LOG("[web] setting otomatis dikembalikan ke default");
  sendConfig();
}

static void hHistory() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "");
  String o; o.reserve(1400);
  char b[48];
  snprintf(b, sizeof(b), "{\"interval\":%d,\"now\":%lu,\"rows\":[", HIST_INTERVAL_S, (unsigned long)(millis() / 1000));
  o += b;
  for (uint16_t i = 0; i < s_histCount; i++) {
    const HistSample& h = histAt(i);
    o += i ? ",[" : "[";
    o += (unsigned long)h.up; o += ',';
    if (h.pm25 >= 0) { snprintf(b, sizeof(b), "%.1f", h.pm25 / 10.0f); o += b; } else o += "null";
    o += ',';
    if (h.eco2 >= 0) o += (int)h.eco2; else o += "null";
    o += ',';
    if (h.temp != INT16_MIN) { snprintf(b, sizeof(b), "%.1f", h.temp / 10.0f); o += b; } else o += "null";
    o += ',';
    if (h.rh >= 0) o += (int)h.rh; else o += "null";
    o += ','; o += (int)h.fan; o += ']';
    if (o.length() > 1200) { server.sendContent(o); o = ""; }
  }
  o += "]}";
  server.sendContent(o);
  server.sendContent("");
}

static void hHistoryCsv() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"cair_riwayat.csv\"");
  server.send(200, "text/csv", "");
  String o = "waktu,uptime_s,pm25_ugm3,pm10_ugm3,eco2_ppm,suhu_c,rh_pct,kipas_pct,rpm\n";
  char b[96];
  for (uint16_t i = 0; i < s_histCount; i++) {
    const HistSample& h = histAt(i);
    char ts[24] = "";
    if (h.epoch) { time_t t = h.epoch; struct tm tmv; localtime_r(&t, &tmv); strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv); }
    char pm25[8] = "", pm10[8] = "", co2[8] = "", tc[8] = "", rh[6] = "";
    if (h.pm25 >= 0) snprintf(pm25, sizeof(pm25), "%.1f", h.pm25 / 10.0f);
    if (h.pm10 >= 0) snprintf(pm10, sizeof(pm10), "%.1f", h.pm10 / 10.0f);
    if (h.eco2 >= 0) snprintf(co2, sizeof(co2), "%d", h.eco2);
    if (h.temp != INT16_MIN) snprintf(tc, sizeof(tc), "%.1f", h.temp / 10.0f);
    if (h.rh >= 0) snprintf(rh, sizeof(rh), "%d", h.rh);
    snprintf(b, sizeof(b), "%s,%lu,%s,%s,%s,%s,%s,%u,%u\n", ts, (unsigned long)h.up, pm25, pm10, co2, tc, rh, h.fan, h.rpm);
    o += b;
    if (o.length() > 1200) { server.sendContent(o); o = ""; }
  }
  server.sendContent(o);
  server.sendContent("");
}

static void hScan() {
  int n = WiFi.scanNetworks(false, false);
  String o = "[";
  int out = 0;
  for (int i = 0; i < n && out < 20; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ss) { dup = true; break; }
    if (dup) continue;
    if (out++) o += ',';
    o += '{'; jStr(o, "ssid", ss.c_str()); o += ',';
    jInt(o, "rssi", WiFi.RSSI(i)); o += ',';
    jBool(o, "lock", WiFi.encryptionType(i) != WIFI_AUTH_OPEN); o += '}';
  }
  o += ']';
  WiFi.scanDelete();
  sendJson(o);
}

static void hWifiPost() {
  String ss = server.arg("ssid"), pw = server.arg("pass");
  if (ss.length() > 32 || pw.length() > 64) { sendErr("SSID/password terlalu panjang"); return; }
  if (pw.length() && pw.length() < 8) { sendErr("password WPA minimal 8 karakter"); return; }
  wifiSetCredentials(ss.c_str(), pw.c_str());
  LOG("[web] SSID router -> \"%s\"", ss.c_str());
  String o = "{"; jBool(o, "ok", true); o += '}';
  sendJson(o);
}

static void hNotFound() {
  // captive portal: semua host lain diarahkan ke dashboard
  String host = server.hostHeader();
  String ap = WiFi.softAPIP().toString();
  if (s_apOn && host != ap && host != String(WIFI_HOSTNAME) + ".local" && host != WiFi.localIP().toString()) {
    server.sendHeader("Location", "http://" + ap + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "404");
}

// ---------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------
void webTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(500));            // beri waktu BLE init lebih dulu
  wifiBegin();

  server.on("/", HTTP_GET, hIndex);
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/mode", HTTP_POST, hMode);
  server.on("/api/fan", HTTP_POST, hFan);
  server.on("/api/config", HTTP_GET, sendConfig);
  server.on("/api/config", HTTP_POST, hConfigPost);
  server.on("/api/config/defaults", HTTP_POST, hConfigDefaults);
  server.on("/api/history", HTTP_GET, hHistory);
  server.on("/api/history.csv", HTTP_GET, hHistoryCsv);
  server.on("/api/wifi/scan", HTTP_GET, hScan);
  server.on("/api/wifi", HTTP_POST, hWifiPost);
  server.onNotFound(hNotFound);
  server.begin();
  LOG("[web] server port %d aktif, heap free %lu B", WEB_PORT, (unsigned long)ESP.getFreeHeap());

  for (;;) {
    uint32_t now = millis();
    if (s_apOn) dns.processNextRequest();
    server.handleClient();
    wifiTick(now);
    historyTick(now);

    if (s_dirtyAt && now - s_dirtyAt > 3000) {
      Settings c; { Lock l; c = g_cfg; }
      bool ok = settingsSave(c);
      LOG("[web] setting %s", ok ? "tersimpan" : "GAGAL disimpan");
      s_dirtyAt = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
