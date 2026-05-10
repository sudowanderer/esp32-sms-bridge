#pragma once

#include <stdint.h>

struct CellularStatusSnapshot {
  bool signalKnown;
  uint8_t csqRssi;
  int16_t rssiDbm;
  uint8_t csqBer;
  bool registrationKnown;
  uint8_t registrationStatus;
  char registrationText[24];
  uint32_t lastUpdatedMs;
};

class CellularStatusCore {
 public:
  static bool parseCsqResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCeregResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static const char* registrationStatusName(uint8_t status);
  static int16_t csqToRssiDbm(uint8_t rssi);
};
