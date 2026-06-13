#pragma once

#include "modem_at.h"

#include <stddef.h>
#include <stdint.h>

struct SmsReceiveWatchdogSnapshot {
  bool cnmiKnown;
  uint8_t cnmiMode;
  uint8_t cnmiMt;
  uint8_t cnmiBm;
  uint8_t cnmiDs;
  uint8_t cnmiBfr;
  char cnmiRaw[24];
  char cnmiDescription[160];
  uint32_t cnmiLastUpdatedMs;
};

class SmsReceiveWatchdogCore {
 public:
  static constexpr uint32_t kConfigRefreshMs = 300000;
  static constexpr uint32_t kStatusQueryMs = 30000;
  static constexpr uint32_t kRetryDelayMs = 30000;

  void begin(uint32_t nowMs);
  void setStartupComplete(bool complete, uint32_t nowMs);
  void onUrc(const char* line, uint32_t nowMs);

  const char* commandToSubmit(uint32_t nowMs) const;
  void markSubmitted(uint32_t nowMs);
  void deferSubmission(uint32_t nowMs);
  void complete(ModemAtResult result, uint32_t nowMs);
  void complete(ModemAtResult result, const char* response, uint32_t nowMs);

  bool pending() const;
  uint32_t lastConfigMs() const;
  uint16_t recoveryCount() const;
  SmsReceiveWatchdogSnapshot snapshot() const;

 private:
  enum class Step : uint8_t {
    PduMode,
    DirectUrcMode,
    QueryUrcMode,
  };

  enum class Work : uint8_t {
    None,
    Configure,
    QueryOnly,
  };

  static const char* modeDescription(uint8_t value);
  static const char* mtDescription(uint8_t value);
  static const char* bmDescription(uint8_t value);
  static const char* dsDescription(uint8_t value);
  static const char* bfrDescription(uint8_t value);
  static bool parseCnmiResponse(const char* response, SmsReceiveWatchdogSnapshot& snapshot, uint32_t nowMs);
  static bool parseUint8(const char*& cursor, uint8_t& value);
  static void copyText(char* dest, size_t destCapacity, const char* source);
  static bool startsWith(const char* value, const char* prefix);
  bool due(uint32_t nowMs) const;
  void requestRecovery(uint32_t nowMs);

  bool startupComplete_ = false;
  bool pending_ = false;
  Step step_ = Step::PduMode;
  Work work_ = Work::None;
  uint32_t nextActionMs_ = 0;
  uint32_t nextConfigMs_ = 0;
  uint32_t nextStatusQueryMs_ = 0;
  uint32_t lastConfigMs_ = 0;
  uint16_t recoveryCount_ = 0;
  SmsReceiveWatchdogSnapshot snapshot_ = {};
};
