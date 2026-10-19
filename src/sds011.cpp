#include "sds011.h"

bool Sds011::feed(uint8_t b, SdsFrame& out) {
  if (_idx == 0) {
    if (b == 0xAA) _buf[_idx++] = b;
    return false;
  }
  if (_idx == 1 && b != 0xC0 && b != 0xC5) {
    _idx = (b == 0xAA) ? 1 : 0;              // resync
    return false;
  }
  _buf[_idx++] = b;
  if (_idx < 10) return false;
  _idx = 0;

  if (_buf[9] != 0xAB) { _bad++; return false; }
  uint8_t cs = 0;
  for (int i = 2; i <= 7; i++) cs += _buf[i];
  if (cs != _buf[8]) { _bad++; return false; }

  out.id = ((uint16_t)_buf[6] << 8) | _buf[7];
  if (_buf[1] == 0xC0) {
    out.isData = true;
    out.pm25 = (float)((uint16_t)_buf[2] | ((uint16_t)_buf[3] << 8)) / 10.0f;
    out.pm10 = (float)((uint16_t)_buf[4] | ((uint16_t)_buf[5] << 8)) / 10.0f;
  } else {
    out.isData   = false;
    out.replyCmd = _buf[2];
    out.r[0] = _buf[3]; out.r[1] = _buf[4]; out.r[2] = _buf[5];
  }
  return true;
}

void Sds011::cmd(uint8_t c, uint8_t d2, uint8_t d3, uint16_t id) {
  if (!_s) return;
  uint8_t f[19] = {0};
  f[0]  = 0xAA;
  f[1]  = 0xB4;
  f[2]  = c;            // Data1
  f[3]  = d2;           // Data2
  f[4]  = d3;           // Data3
  f[15] = id >> 8;      // Data14
  f[16] = id & 0xFF;    // Data15
  uint8_t cs = 0;
  for (int i = 2; i <= 16; i++) cs += f[i];
  f[17] = cs;
  f[18] = 0xAB;
  _s->write(f, sizeof(f));
  _s->flush();
}
