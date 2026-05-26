#include "modem_events_core.h"

#include <string.h>

void ModemEventsCore::begin() {
  snapshot_ = {};
  snapshot_.simState = ModemSimState::Unknown;
}

bool ModemEventsCore::onUrc(const char* line, uint32_t nowMs) {
  if (line == nullptr) {
    return false;
  }

  poll(nowMs);

  if (strcmp(line, "+MATREADY") == 0) {
    snapshot_.matreadyCount++;
    snapshot_.lastMatreadyMs = nowMs;
    copyEvent(snapshot_.lastEvent, "matready");
    return true;
  }

  if (startsWith(line, "+CPIN: READY")) {
    snapshot_.simState = ModemSimState::Ready;
    snapshot_.cpinReadyCount++;
    snapshot_.lastCpinReadyMs = nowMs;
    copyEvent(snapshot_.lastEvent, "cpin_ready");
    return true;
  }

  if (startsWith(line, "+CPIN: SIM REMOVED")) {
    snapshot_.simRemovedCount++;
    snapshot_.lastSimRemovedMs = nowMs;
    if (snapshot_.cpinReadyCount > 0 &&
        static_cast<uint32_t>(nowMs - snapshot_.lastCpinReadyMs) <= kCpinPairWindowMs) {
      snapshot_.simState = ModemSimState::Ready;
    } else {
      snapshot_.simState = ModemSimState::SuspectRemoved;
    }
    copyEvent(snapshot_.lastEvent, "sim_removed");
    return true;
  }

  return false;
}

void ModemEventsCore::poll(uint32_t nowMs) {
  if (snapshot_.simState == ModemSimState::SuspectRemoved &&
      static_cast<uint32_t>(nowMs - snapshot_.lastSimRemovedMs) >= kCpinPairWindowMs) {
    snapshot_.simState = ModemSimState::Removed;
  }
}

ModemEventsSnapshot ModemEventsCore::snapshot() const {
  return snapshot_;
}

const char* ModemEventsCore::simStateName(ModemSimState state) {
  switch (state) {
    case ModemSimState::Ready:
      return "ready";
    case ModemSimState::SuspectRemoved:
      return "suspect_removed";
    case ModemSimState::Removed:
      return "removed";
    case ModemSimState::Unknown:
      return "unknown";
  }
  return "unknown";
}

bool ModemEventsCore::startsWith(const char* value, const char* prefix) {
  return value != nullptr && prefix != nullptr && strncmp(value, prefix, strlen(prefix)) == 0;
}

void ModemEventsCore::copyEvent(char* output, const char* event) {
  if (output == nullptr) {
    return;
  }

  strncpy(output, event != nullptr ? event : "", sizeof(ModemEventsSnapshot::lastEvent) - 1);
  output[sizeof(ModemEventsSnapshot::lastEvent) - 1] = '\0';
}
