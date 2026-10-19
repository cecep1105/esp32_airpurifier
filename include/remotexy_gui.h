#pragma once
// =====================================================================
//  RemoteXY GUI - HANYA di-include oleh src/remotexy_task.cpp
//  ESP32-S3 hanya punya BLE (tidak ada Bluetooth Classic), jadi di
//  editor RemoteXY pilih: Connection = Bluetooth / BLE, Board = ESP32,
//  Module = ESP32 BLE (built-in).
// =====================================================================
#include "config.h"

#define REMOTEXY_MODE__ESP32CORE_BLE
#include <BLEDevice.h>
#define REMOTEXY_BLUETOOTH_NAME RXY_BLE_NAME
#ifdef RXY_PASSWORD
  #define REMOTEXY_ACCESS_PASSWORD RXY_PASSWORD
#endif
#include <RemoteXY.h>

#pragma pack(push, 1)

// GUI "airpurifier" dari editor RemoteXY (library 4.1.10+).
// Jika GUI diubah di editor, ganti CONF dan struct di bawah bersamaan.
uint8_t const PROGMEM RemoteXY_CONF_PROGMEM[] =   // 406 bytes V19 , PRO license required
  { 255,3,0,30,0,143,1,19,0,0,0,0,174,1,106,200,1,1,33,0,
  130,1,19,104,77,27,2,129,4,1,25,9,64,17,83,109,97,114,116,0,
  129,14,8,56,12,64,17,65,105,114,32,80,117,114,105,102,105,101,114,0,
  72,37,30,64,64,12,149,255,2,26,0,0,0,0,0,0,200,66,0,0,
  0,0,129,5,23,40,5,64,246,75,85,65,76,73,84,65,83,32,85,68,
  65,82,65,0,67,4,38,17,10,74,24,2,67,48,54,42,16,65,8,11,
  129,7,77,16,6,64,17,80,77,49,48,0,67,252,83,27,10,74,24,2,
  129,5,32,18,6,64,17,80,77,50,46,53,0,130,1,98,104,28,27,17,
  2,39,109,44,13,1,12,250,31,31,65,85,84,79,0,77,65,78,85,65,
  76,0,129,5,102,25,5,64,246,77,79,68,69,32,70,65,78,0,130,1,
  128,104,27,27,17,1,64,137,14,11,3,2,31,43,0,1,82,138,14,11,
  3,2,31,45,0,129,5,131,33,5,64,244,75,69,67,69,80,65,78,32,
  70,65,78,0,67,3,143,30,9,89,24,130,1,157,103,40,27,17,129,14,
  160,14,5,64,244,83,85,72,85,0,129,86,160,8,6,64,244,82,72,0,
  129,23,85,17,6,64,24,194,181,103,47,109,194,179,0,129,21,40,17,6,
  64,24,194,181,103,47,109,194,179,0,67,5,167,17,8,74,24,2,67,65,
  168,24,9,90,24,129,25,169,7,6,64,24,194,176,67,0,129,91,169,6,
  7,64,24,37,0,129,37,139,11,5,64,244,82,80,77,0,129,12,138,15,
  5,64,244,76,69,86,69,76,0,67,27,143,30,9,81,24,129,46,160,16,
  6,64,244,101,67,79,50,0,129,58,168,13,6,64,8,112,112,109,0,67,
  36,168,18,7,82,8 };

// this structure defines all the variables and events of your control interface
struct {

    // input variables
  uint8_t switch_fanmode; // =1 if switch ON and =0 if OFF, from 0 to 1
  uint8_t button_fanadd; // =1 if button pressed, else =0, from 0 to 1
  uint8_t button_fansub; // =1 if button pressed, else =0, from 0 to 1

    // output variables
  int8_t circularBar_01; // from 0 to 100
  float value_pm25;
  char value_status[11]; // string UTF8 end zero
  float value_pm10;
  int8_t value_fanspeed; // -128 .. 127
  float value_temp;
  int8_t value_rh; // -128 .. 127
  int16_t value_fanrpm; // -32768 .. +32767
  int16_t value_CO2; // -32768 .. +32767

    // other variable
  uint8_t connect_flag;  // =1 if wire connected, else =0

} RemoteXY;
#pragma pack(pop)
