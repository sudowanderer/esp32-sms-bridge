#pragma once

#include <stdint.h>

enum class ModemSimState : uint8_t {
  Unknown,
  Ready,
  SuspectRemoved,
  Removed,
};

struct ModemEventsSnapshot {
  ModemSimState simState = ModemSimState::Unknown;
  char lastEvent[24] = {};
  uint16_t matreadyCount = 0;
  uint16_t cpinReadyCount = 0;
  uint16_t simRemovedCount = 0;
  uint32_t lastMatreadyMs = 0;
  uint32_t lastCpinReadyMs = 0;
  uint32_t lastSimRemovedMs = 0;
};

class ModemEventsCore {
 public:
  static constexpr uint32_t kCpinPairWindowMs = 5000;

  void begin();
  bool onUrc(const char* line, uint32_t nowMs);
  void poll(uint32_t nowMs);
  ModemEventsSnapshot snapshot() const;

  static const char* simStateName(ModemSimState state);

 private:
  static bool startsWith(const char* value, const char* prefix);
  static void copyEvent(char* output, const char* event);

  ModemEventsSnapshot snapshot_ = {};
};
