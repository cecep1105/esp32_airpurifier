// =====================================================================
//  CAIR - Smarter Air Purifier (ESP32-S3 + FreeRTOS)
//  Task:
//    sensor  (core1, prio 3) : SDS011 (UART1 S3 / UART2 D1 R32), parse, moving average
//    fan     (core1, prio 4) : AUTO/POT/FIXED, slew, PWM 25 kHz, LED, tombol
//    display (core0, prio 1) : OLED SSD1306 128x64, 4 Hz
//    console (core0, prio 2) : UART0 CLI + log status
//    remotexy(core0, prio 2) : RemoteXY via BLE (app HP)
//    env     (core1, prio 2) : ENS160 + AHT21 (I2C), 1 Hz
//    web     (core0, prio 2) : WiFi AP/STA + web dashboard + riwayat
// =====================================================================
#include "app.h"
#include "log.h"
#include <esp_system.h>
#include <Wire.h>

SemaphoreHandle_t g_mtx;
Settings          g_cfg;
SensorData        g_sensor;
FanState          g_fan;
QueueHandle_t     g_sdsQ;
volatile bool     g_displayOk = false;
EnvData           g_env;
SemaphoreHandle_t g_i2cMtx;
volatile bool     g_bleConnected = false;

static const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC/EXCEPTION";
    case ESP_RST_INT_WDT:   return "INT WATCHDOG";
    case ESP_RST_TASK_WDT:  return "TASK WATCHDOG";
    case ESP_RST_WDT:       return "OTHER WATCHDOG";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_EXT:       return "EXTERNAL PIN";
    default:                return "UNKNOWN";
  }
}

static void startTask(TaskFunction_t fn, const char* name, uint32_t stack, UBaseType_t prio, BaseType_t core) {
  BaseType_t ok = xTaskCreatePinnedToCore(fn, name, stack, nullptr, prio, nullptr, core);
  LOG("[task] %-8s core=%d prio=%u stack=%lu -> %s", name, (int)core, (unsigned)prio,
      (unsigned long)stack, ok == pdPASS ? "OK" : "GAGAL");
}

void setup() {
  LOG_SERIAL.begin(LOG_BAUD, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  delay(50);
  LOG_SERIAL.println();
  LOG_SERIAL.println("==================================================");
  LOG_SERIAL.println(" CAIR - Smarter Air Purifier (" BOARD_NAME " + FreeRTOS)");
  LOG_SERIAL.printf (" build %s %s\n", __DATE__, __TIME__);
  LOG_SERIAL.println("==================================================");

  LOG("[sys] reset   : %s", resetReasonStr(esp_reset_reason()));
  LOG("[sys] board   : %s", BOARD_NAME);
  LOG("[sys] fitur   : RemoteXY BLE %s, web WiFi %s", ENABLE_REMOTEXY ? "ON" : "OFF", ENABLE_WEB ? "ON" : "OFF");
  LOG("[sys] chip    : %s rev%d, %d core, %lu MHz", ESP.getChipModel(), ESP.getChipRevision(),
      ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  LOG("[sys] flash   : %lu KB, PSRAM %lu KB", (unsigned long)(ESP.getFlashChipSize() / 1024),
      (unsigned long)(ESP.getPsramSize() / 1024));
  LOG("[sys] heap    : free %lu / %lu B", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getHeapSize());
  LOG("[sys] SDK     : %s", ESP.getSdkVersion());
  LOG("[pin] UART0 console RX=%d TX=%d @%d", PIN_LOG_RX, PIN_LOG_TX, LOG_BAUD);
  LOG("[pin] SDS011 RX=%d TX=%d @%d", PIN_SDS_RX, PIN_SDS_TX, SDS_BAUD);
  LOG("[pin] I2C SDA=%d SCL=%d OLED=0x%02X", PIN_I2C_SDA, PIN_I2C_SCL, OLED_ADDR);
  LOG("[pin] FAN PWM=%d (%d Hz, %d bit)  TACH=%d  RELAY=%d", PIN_FAN_PWM, FAN_PWM_FREQ, FAN_PWM_RES,
      PIN_FAN_TACH, PIN_FAN_RELAY);
  LOG("[pin] POT=%d  BTN=%d  LED=%d (%s)", PIN_POT, PIN_BTN_MODE, PIN_RGB, LED_WS2812 ? "WS2812" : "satu warna");

  g_mtx    = xSemaphoreCreateMutex();
  g_i2cMtx = xSemaphoreCreateMutex();
  g_sdsQ   = xQueueCreate(4, sizeof(SdsReq));
  LOG("[rtos] mutex %s, i2c %s, queue %s", g_mtx ? "OK" : "GAGAL", g_i2cMtx ? "OK" : "GAGAL",
      g_sdsQ ? "OK" : "GAGAL");

  // I2C bersama: OLED 0x3C, AHT21 0x38, ENS160 0x53/0x52
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      const char* n = a == OLED_ADDR ? "OLED" : a == 0x38 ? "AHT21" : (a == 0x53 || a == 0x52) ? "ENS160" : "?";
      LOG("[i2c] device di 0x%02X (%s)", a, n);
      found++;
    }
  }
  if (!found) LOG("[i2c] tidak ada device, cek SDA=%d SCL=%d & VCC", PIN_I2C_SDA, PIN_I2C_SCL);

  if (settingsLoad(g_cfg)) {
    LOG("[cfg] dimuat dari NVS");
  } else {
    settingsDefaults(g_cfg);
    LOG("[cfg] NVS kosong/beda versi, pakai default");
  }
  LOG("[cfg] mode=%s th=%.1f/%.1f/%.1f duty=%u/%u/%u/%u fixed=%u fault=%u",
      modeName(g_cfg.mode), g_cfg.th[0], g_cfg.th[1], g_cfg.th[2],
      g_cfg.duty[0], g_cfg.duty[1], g_cfg.duty[2], g_cfg.duty[3], g_cfg.fixedDuty, g_cfg.faultDuty);
  LOG("[cfg] hyst=%.1f slew=%u%%/s avg=%u dead=%u%% sds=%s/%us",
      g_cfg.hyst, g_cfg.slewPctPerS, g_cfg.avgWindow, g_cfg.potDeadband,
      g_cfg.sdsQueryMode ? "query" : "active", g_cfg.sdsQueryIntervalS);

  startTask(fanTask,     "fan",     4096, 4, 1);
  startTask(sensorTask,  "sensor",  4096, 3, 1);
  startTask(consoleTask, "console", 4096, 2, 0);
  startTask(displayTask, "display", 4096, 1, 0);
  startTask(envTask,     "env",     4096, 2, 1);
#if ENABLE_REMOTEXY
  startTask(remotexyTask, "remotexy", 8192, 2, 0);
#endif
#if ENABLE_WEB
  startTask(webTask,     "web",     8192, 2, 0);
#endif
  LOG("[sys] setup selesai, heap free %lu B", (unsigned long)ESP.getFreeHeap());
}

void loop() {
  vTaskDelete(nullptr);   // semua kerja di task FreeRTOS
}
