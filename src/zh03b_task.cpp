// =====================================================================
//  ZH03B (Winsen) - PM1.0 / PM2.5 / PM10 LUAR RUANGAN, UART 9600 8N1
//  Mode initiative upload (default), frame 24 byte tiap ±1 s:
//    [0]=0x42 [1]=0x4D [2..3]=panjang 0x0014 [4..9]=reserved
//    [10..11]=PM1.0  [12..13]=PM2.5  [14..15]=PM10   (ug/m3, high byte dulu)
//    [16..21]=reserved  [22..23]=checksum = jumlah byte 0..21 (16 bit)
//  Daya 5 V, logika UART 3.3 V (aman langsung ke ESP32).
// =====================================================================
#include "app.h"
#include "log.h"

#if ENABLE_ZH03B

class Zh03b {
public:
  // true bila frame valid lengkap
  bool feed(uint8_t b) {
    if (_idx == 0) { if (b == 0x42) _buf[_idx++] = b; return false; }
    if (_idx == 1) {
      if (b == 0x4D) _buf[_idx++] = b;
      else _idx = (b == 0x42) ? 1 : 0;
      return false;
    }
    _buf[_idx++] = b;
    if (_idx < 24) return false;
    _idx = 0;
    uint16_t len = ((uint16_t)_buf[2] << 8) | _buf[3];
    uint16_t sum = 0;
    for (int i = 0; i < 22; i++) sum += _buf[i];
    uint16_t cs = ((uint16_t)_buf[22] << 8) | _buf[23];
    if (len != 0x0014 || sum != cs) { bad++; return false; }
    pm1  = ((uint16_t)_buf[10] << 8) | _buf[11];
    pm25 = ((uint16_t)_buf[12] << 8) | _buf[13];
    pm10 = ((uint16_t)_buf[14] << 8) | _buf[15];
    return true;
  }
  uint16_t pm1 = 0, pm25 = 0, pm10 = 0;
  uint32_t bad = 0;
private:
  uint8_t _buf[24];
  uint8_t _idx = 0;
};

void zh03bTask(void*) {
  ZH_SERIAL.begin(ZH_BAUD, SERIAL_8N1, PIN_ZH_RX, PIN_ZH_TX);
  vTaskDelay(pdMS_TO_TICKS(500));
  while (ZH_SERIAL.available()) ZH_SERIAL.read();

  // pastikan mode initiative upload (bila sebelumnya diset Q&A)
  static const uint8_t CMD_INITIATIVE[9] = {0xFF, 0x01, 0x78, 0x40, 0x00, 0x00, 0x00, 0x00, 0x47};
  ZH_SERIAL.write(CMD_INITIATIVE, sizeof(CMD_INITIATIVE));
  LOG("[zh] ZH03B (PM luar) RX=%d TX=%d @%d, menunggu frame...", PIN_ZH_RX, PIN_ZH_TX, ZH_BAUD);

  Zh03b zh;
  float    ring[AVG_MAX] = {0};
  uint8_t  head = 0, count = 0;
  bool     online = false;
  uint32_t lastBad = 0;

  for (;;) {
    while (ZH_SERIAL.available()) {
      if (!zh.feed((uint8_t)ZH_SERIAL.read())) continue;

      uint8_t win;
      { Lock l; win = g_cfg.avgWindow; }
      ring[head] = zh.pm25;
      head = (head + 1) % AVG_MAX;
      if (count < AVG_MAX) count++;
      uint8_t n = min(win, count);
      float sum = 0;
      for (uint8_t i = 0; i < n; i++) sum += ring[(head + AVG_MAX - 1 - i) % AVG_MAX];

      if (!online) { online = true; LOG("[zh] ONLINE, PM2.5 luar=%u PM10=%u", zh.pm25, zh.pm10); }
      LOGV("[zh] PM1=%u PM2.5=%u PM10=%u", zh.pm1, zh.pm25, zh.pm10);

      Lock l;
      g_out.pm1       = zh.pm1;
      g_out.pm25      = zh.pm25;
      g_out.pm10      = zh.pm10;
      g_out.pm25Avg   = sum / n;
      g_out.lastMs    = millis();
      g_out.everValid = true;
      g_out.frames++;
    }

    if (zh.bad != lastBad) { lastBad = zh.bad; LOGV("[zh] frame rusak (total %lu)", (unsigned long)lastBad); }
    bool fresh;
    { Lock l; g_out.errors = zh.bad; fresh = outFresh(g_out, millis()); }
    if (online && !fresh) { online = false; LOG("[zh] OFFLINE, tidak ada frame > %d ms", OUT_TIMEOUT_MS); }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

#endif  // ENABLE_ZH03B
