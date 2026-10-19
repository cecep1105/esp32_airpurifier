#include "app.h"
#include "log.h"
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

// Layout 128x64:
//  y0  CAIR AUTO      W B OK
//  y13 PM2.5  [ 23.4 ]  (size 2)
//  y33 PM10  35.0 CO2  650
//  y44 BAHAYA     28.5C 65%
//  y55 FAN 75% 1234rpm [###]
// Wire.begin() & scan I2C dilakukan di setup(); semua akses bus via I2CLock.
void displayTask(void*) {
  bool ok;
  { I2CLock l; ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR); }
  if (!ok) {
    g_displayOk = false;
    LOG("[oled] GAGAL init SSD1306 di 0x%02X, task display berhenti", OLED_ADDR);
    vTaskDelete(nullptr);
  }
  g_displayOk = true;
  LOG("[oled] SSD1306 %dx%d OK", OLED_W, OLED_H);

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(40, 16); oled.print("CAIR");
  oled.setTextSize(1);
  oled.setCursor(10, 40); oled.print("Smart Air Purifier");
  { I2CLock l; oled.display(); }
  vTaskDelay(pdMS_TO_TICKS(1500));

  for (;;) {
    Settings s; SensorData sd; FanState fs; EnvData e;
    { Lock l; s = g_cfg; sd = g_sensor; fs = g_fan; e = g_env; }
    uint32_t now = millis();
    bool fresh  = sensorFresh(sd, s, now);
    bool thOk   = envThFresh(e, now);
    bool co2Ok  = e.ensOk && e.ensMs && (now - e.ensMs) < 10000;
    bool wifiOk = WiFi.status() == WL_CONNECTED;

    oled.clearDisplay();
    oled.setTextSize(1);

    // header
    oled.setCursor(0, 0);
    oled.printf("CAIR %s", modeName(s.mode));
    char st[12];
    snprintf(st, sizeof(st), "%s%s%s", wifiOk ? "W " : "", g_bleConnected ? "B " : "",
             sd.sleeping ? "SLP" : (fresh ? (sd.queryMode ? "Q" : "OK") : "--"));
    oled.setCursor(128 - 6 * strlen(st), 0);
    oled.print(st);
    oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);

    // PM2.5 besar
    oled.setCursor(0, 13); oled.print("PM2.5");
    oled.setCursor(0, 22); oled.print("ug/m3");
    oled.setTextSize(2);
    oled.setCursor(44, 13);
    if (fresh) oled.printf("%5.1f", sd.pm25Avg);
    else       oled.print(" --.-");
    oled.setTextSize(1);

    // PM10 + eCO2
    oled.setCursor(0, 33);
    if (fresh) oled.printf("PM10%6.1f", sd.pm10);
    else       oled.print("PM10  --.-");
    oled.setCursor(68, 33);
    if (!co2Ok)                  oled.print("CO2  ---");
    else if (e.ensValidity == 1) oled.print("CO2 WARM");
    else                         oled.printf("CO2%5u", e.eco2);

    // kategori + suhu/RH
    oled.setCursor(0, 44);
    if (sd.sleeping)           oled.print("SNS SLEEP");
    else if (!sd.everValid)    oled.print("TUNGGU...");
    else if (!fresh)           oled.print("SNS ERROR");
    else                       oled.print(categoryName(fs.category));
    oled.setCursor(74, 44);
    if (thOk) oled.printf("%4.1fC%3.0f%%", e.t, e.rh);
    else      oled.print("--.-C --%");

    // kipas
    int pct = (int)lroundf(fs.outPct);
    oled.setCursor(0, 55);
    if (!fs.relayOn)     oled.print("FAN OFF");
    else if (fs.stalled) oled.printf("FAN%3d%% MACET", pct);
    else                 oled.printf("FAN%3d%% %4urpm", pct, fs.rpm);
    oled.drawRect(94, 54, 34, 9, SSD1306_WHITE);
    int w = (30 * pct) / 100;
    if (w > 0) oled.fillRect(96, 56, w, 5, SSD1306_WHITE);

    { I2CLock l; oled.display(); }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
