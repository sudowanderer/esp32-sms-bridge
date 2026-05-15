#pragma once

#include <stdint.h>

struct CellularStatusSnapshot {
  static constexpr uint8_t kMaxPdpContexts = 8;

  struct PdpContext {
    uint8_t cid;
    bool known;
    bool activationKnown;
    bool active;
    bool ignored;
    char apn[32];
  };

  bool signalKnown;
  uint8_t csqRssi;
  int16_t rssiDbm;
  uint8_t csqBer;
  bool registrationKnown;
  uint8_t registrationStatus;
  char registrationText[24];
  bool moduleInfoKnown;
  char manufacturer[24];
  char model[24];
  char firmware[64];
  bool lteSignalKnown;
  uint8_t cesqRxlev;
  uint8_t cesqBer;
  uint8_t cesqRscp;
  uint8_t cesqEcno;
  uint8_t cesqRsrq;
  uint8_t cesqRsrp;
  int16_t rsrpDbm;
  int16_t rsrqDbTenths;
  char cesqRaw[32];
  char imsi[24];
  char iccid[32];
  char ownNumber[32];
  char operatorName[32];
  bool dataConnectionKnown;
  bool dataConnectionActive;
  bool mipCallKnown;
  bool mipCallActive;
  char apn[64];
  PdpContext pdpContexts[kMaxPdpContexts];
  uint8_t pdpContextCount;
  uint32_t lastUpdatedMs;
};

class CellularStatusCore {
 public:
  static bool parseCsqResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCeregResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseAtiResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCesqResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseImsiResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseIccidResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCnumResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCopsResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCgactResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseCgdccontResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static bool parseMipCallResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs);
  static const char* registrationStatusName(uint8_t status);
  static const char* rsrpQualityName(int16_t rsrpDbm, bool known);
  static int16_t csqToRssiDbm(uint8_t rssi);
};
