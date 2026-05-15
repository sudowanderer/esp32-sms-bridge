#pragma once

#include "modem_at.h"

#include <stddef.h>
#include <stdint.h>

class PdpGuardCore {
 public:
  static constexpr uint32_t kCommandTimeoutMs = 10000;
  static constexpr uint32_t kRetryIntervalMs = 60000;

  void begin();
  void setStartupComplete(bool complete, uint32_t nowMs);
  const char* commandToSubmit(uint32_t nowMs);
  void markSubmitted();
  void complete(ModemAtResult result, const char* response, uint32_t nowMs);
  void deferSubmission(uint32_t nowMs);

  bool isPending() const;
  bool isDeactivated() const;
  bool isAlreadyDisconnected() const;

 private:
  enum class State : uint8_t {
    QueryConnection,
    Disconnect,
    Complete,
  };

  void scheduleRetry(uint32_t nowMs);
  bool parseConnectionActive(const char* response, bool& active) const;

  bool startupComplete_ = false;
  bool pending_ = false;
  bool deactivated_ = false;
  bool alreadyDisconnected_ = false;
  uint32_t nextAttemptMs_ = 0;
  State state_ = State::QueryConnection;
};
