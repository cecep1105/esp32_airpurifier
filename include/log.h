#pragma once
#include <Arduino.h>
#include "config.h"

// Log ke UART0 (LOG_SERIAL) dengan timestamp. LOG_VERBOSE=1 -> log tiap frame sensor & reply.
#define LOG(fmt, ...)  LOG_SERIAL.printf("[%7lu] " fmt "\n", (unsigned long)millis(), ##__VA_ARGS__)
#if LOG_VERBOSE
  #define LOGV(fmt, ...) LOG(fmt, ##__VA_ARGS__)
#else
  #define LOGV(fmt, ...) do {} while (0)
#endif
