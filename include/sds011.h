#pragma once
#include <Arduino.h>

// Driver protokol Nova SDS011 (laser PM2.5/PM10)
//  Data   : AA C0 PM25L PM25H PM10L PM10H ID1 ID2 CS AB      (10 byte)
//  Reply  : AA C5 CMD  D2    D3    D4    ID1 ID2 CS AB      (10 byte)
//  Command: AA B4 CMD  D2..D13 ID1 ID2 CS AB                (19 byte)
//  CS = jumlah byte data (index 2..n-3) & 0xFF

struct SdsFrame {
  bool     isData;     // true: frame C0 (data), false: reply C5
  float    pm25;
  float    pm10;
  uint16_t id;
  uint8_t  replyCmd;
  uint8_t  r[3];
};

class Sds011 {
public:
  void begin(HardwareSerial& s) { _s = &s; }
  bool feed(uint8_t b, SdsFrame& out);           // true jika frame valid lengkap

  void setReportingMode(bool query) { cmd(2, 1, query ? 1 : 0); }
  void setWorking(bool work)        { cmd(6, 1, work ? 1 : 0); }
  void setPeriod(uint8_t minutes)   { cmd(8, 1, minutes); }
  void query()                      { cmd(4, 0, 0); }

  uint32_t badFrames() const { return _bad; }

private:
  void cmd(uint8_t c, uint8_t d2, uint8_t d3, uint16_t id = 0xFFFF);
  HardwareSerial* _s = nullptr;
  uint8_t  _buf[10];
  uint8_t  _idx = 0;
  uint32_t _bad = 0;
};
