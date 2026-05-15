#pragma once

#include "modem_at.h"

#include <stddef.h>
#include <stdint.h>

class PdpGuardCore {
 public:
  static constexpr uint32_t kCommandTimeoutMs = 10000;
  static constexpr uint32_t kRetryIntervalMs = 60000;
  static constexpr uint8_t kMaxContexts = 8;
  static constexpr size_t kCommandCapacity = 32;

  void begin();
  void setStartupComplete(bool complete, uint32_t nowMs);
  const char* commandToSubmit(uint32_t nowMs);
  void markSubmitted();
  void complete(ModemAtResult result, const char* response, uint32_t nowMs);
  void deferSubmission(uint32_t nowMs);

  bool isPending() const;
  bool isDeactivated() const;
  bool hasOnlyIgnoredContextsActive() const;
  uint8_t lastTargetCid() const;

 private:
  enum class State : uint8_t {
    QueryContexts,
    QueryActivation,
    Deactivate,
    Complete,
  };

  struct Context {
    uint8_t cid = 0;
    bool known = false;
    bool active = false;
    bool ignored = false;
  };

  void scheduleRetry(uint32_t nowMs);
  void resetContexts();
  bool parseContexts(const char* response);
  bool parseActivation(const char* response);
  int findTargetIndex();
  Context* findOrAddContext(uint8_t cid);

  bool startupComplete_ = false;
  bool pending_ = false;
  bool deactivated_ = false;
  bool onlyIgnoredContextsActive_ = false;
  uint32_t nextAttemptMs_ = 0;
  State state_ = State::QueryContexts;
  Context contexts_[kMaxContexts];
  uint8_t contextCount_ = 0;
  uint8_t targetCid_ = 0;
  char command_[kCommandCapacity];
};
