#include "app.h"
#include "sds011.h"
#include "log.h"

// UART1 -> SDS011 (SDS_SERIAL, lihat config.h)
void sensorTask(void*) {
  SDS_SERIAL.begin(SDS_BAUD, SERIAL_8N1, PIN_SDS_RX, PIN_SDS_TX);
  Sds011 sds;
  sds.begin(SDS_SERIAL);

  vTaskDelay(pdMS_TO_TICKS(1000));            // tunggu sensor/simulator siap
  while (SDS_SERIAL.available()) SDS_SERIAL.read();  // buang byte sisa

  bool queryMode;
  uint16_t qInt;
  { Lock l; queryMode = g_cfg.sdsQueryMode; qInt = g_cfg.sdsQueryIntervalS; }

  LOG("[sds] UART1 aktif @%d, kirim init: work, period 0, mode %s", SDS_BAUD, queryMode ? "query" : "active");
  sds.setWorking(true);            vTaskDelay(pdMS_TO_TICKS(100));
  sds.setPeriod(0);                vTaskDelay(pdMS_TO_TICKS(100));
  sds.setReportingMode(queryMode); vTaskDelay(pdMS_TO_TICKS(100));
  { Lock l; g_sensor.queryMode = queryMode; g_sensor.sleeping = false; }
  LOG("[sds] menunggu frame data...");
  bool online = false;
  uint32_t lastFrameMs = 0;

  float    ring[AVG_MAX] = {0};
  uint8_t  rHead = 0, rCount = 0;
  bool     sleeping = false;
  uint32_t lastQuery = 0;

  for (;;) {
    // --- permintaan dari console ---
    SdsReq req;
    while (xQueueReceive(g_sdsQ, &req, 0) == pdTRUE) {
      switch (req.type) {
        case SDS_REQ_APPLY_MODE:
          { Lock l; queryMode = g_cfg.sdsQueryMode; }
          sds.setReportingMode(queryMode);
          LOG("[sds] mode -> %s", queryMode ? "query" : "active");
          { Lock l; g_sensor.queryMode = queryMode; }
          lastQuery = 0;
          break;
        case SDS_REQ_SLEEP:
          sds.setWorking(false); sleeping = true; online = false;
          LOG("[sds] SLEEP");
          { Lock l; g_sensor.sleeping = true; }
          break;
        case SDS_REQ_WAKE:
          sds.setWorking(true); sleeping = false;
          LOG("[sds] WAKE");
          { Lock l; g_sensor.sleeping = false; }
          break;
        case SDS_REQ_QUERY:
          sds.query();
          LOGV("[sds] query manual");
          break;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    // --- baca & parse ---
    SdsFrame f;
    while (SDS_SERIAL.available()) {
      if (!sds.feed((uint8_t)SDS_SERIAL.read(), f)) continue;
      if (!f.isData) {
        LOGV("[sds] reply cmd=%u data=%u,%u,%u id=%04X", f.replyCmd, f.r[0], f.r[1], f.r[2], f.id);
        continue;
      }
      lastFrameMs = millis();
      if (!online) {
        online = true;
        LOG("[sds] ONLINE, device id %04X, PM2.5=%.1f PM10=%.1f", f.id, f.pm25, f.pm10);
      }
      LOGV("[sds] frame PM2.5=%.1f PM10=%.1f", f.pm25, f.pm10);

      uint8_t win;
      { Lock l; win = g_cfg.avgWindow; }
      ring[rHead] = f.pm25;
      rHead = (rHead + 1) % AVG_MAX;
      if (rCount < AVG_MAX) rCount++;

      uint8_t n = min(win, rCount);
      float sum = 0;
      for (uint8_t i = 0; i < n; i++) sum += ring[(rHead + AVG_MAX - 1 - i) % AVG_MAX];

      Lock l;
      g_sensor.pm25      = f.pm25;
      g_sensor.pm10      = f.pm10;
      g_sensor.pm25Avg   = sum / n;
      g_sensor.devId     = f.id;
      g_sensor.lastMs    = millis();
      g_sensor.everValid = true;
      g_sensor.frames++;
    }

    static uint32_t lastBad = 0;
    if (sds.badFrames() != lastBad) {
      lastBad = sds.badFrames();
      LOGV("[sds] frame rusak/checksum salah (total %lu)", (unsigned long)lastBad);
    }

    bool fresh;
    { Lock l; g_sensor.errors = sds.badFrames(); qInt = g_cfg.sdsQueryIntervalS; fresh = sensorFresh(g_sensor, g_cfg, millis()); }
    if (online && !fresh && !sleeping) {
      online = false;
      LOG("[sds] OFFLINE, tidak ada frame selama %lu ms", (unsigned long)(millis() - lastFrameMs));
    }

    // --- query mode: minta data berkala ---
    uint32_t now = millis();
    if (queryMode && !sleeping && (lastQuery == 0 || now - lastQuery >= (uint32_t)qInt * 1000UL)) {
      sds.query();
      lastQuery = now;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
