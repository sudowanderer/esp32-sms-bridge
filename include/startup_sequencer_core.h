#pragma once

#include "modem_at.h"

#include <stdint.h>

class StartupSequencerCore {
 public:
  static constexpr uint32_t kRetryDelayMs = 1000;
  static constexpr uint32_t kDefaultCommandTimeoutMs = 3000;
  static constexpr uint8_t kMaxAttemptsPerCommand = 2;

  static uint32_t timeoutForCommand(const char* command);

  void begin(uint32_t nowMs);
  const char* commandToSubmit(uint32_t nowMs);
  void markSubmitted(uint32_t nowMs);
  void deferSubmission(uint32_t nowMs);
  void complete(ModemAtResult result, uint32_t nowMs);
  void onUrc(const char* line, uint32_t nowMs);

  bool isComplete() const;
  bool hasMatreadySeen() const;

 private:
  enum class State : uint8_t {
    Command,
    Complete,
  };

  void advance(uint32_t nowMs);
  bool commandCanRun(uint32_t nowMs) const;

  State state_ = State::Command;
  uint8_t commandIndex_ = 0;
  uint8_t attempts_ = 0;
  bool pending_ = false;
  bool matreadySeen_ = false;
  uint32_t nextActionMs_ = 0;
};
