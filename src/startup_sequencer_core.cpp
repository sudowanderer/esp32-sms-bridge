#include "startup_sequencer_core.h"

#include "startup_commands.h"

#include <string.h>

uint32_t StartupSequencerCore::timeoutForCommand(const char* command) {
  (void)command;
  return kDefaultCommandTimeoutMs;
}

void StartupSequencerCore::begin(uint32_t nowMs) {
  state_ = State::Command;
  commandIndex_ = 0;
  attempts_ = 0;
  pending_ = false;
  matreadySeen_ = false;
  nextActionMs_ = nowMs;
}

const char* StartupSequencerCore::commandToSubmit(uint32_t nowMs) {
  if (!commandCanRun(nowMs)) {
    return nullptr;
  }

  return StartupCommands::at(commandIndex_);
}

void StartupSequencerCore::markSubmitted(uint32_t nowMs) {
  (void)nowMs;
  pending_ = true;
  attempts_++;
}

void StartupSequencerCore::deferSubmission(uint32_t nowMs) {
  if (!pending_) {
    nextActionMs_ = nowMs + kRetryDelayMs;
  }
}

void StartupSequencerCore::complete(ModemAtResult result, uint32_t nowMs) {
  if (!pending_) {
    return;
  }

  pending_ = false;
  if (state_ != State::Command) {
    return;
  }

  if (result == ModemAtResult::Ok || attempts_ >= kMaxAttemptsPerCommand) {
    advance(nowMs);
    return;
  }

  nextActionMs_ = nowMs + kRetryDelayMs;
}

void StartupSequencerCore::onUrc(const char* line, uint32_t nowMs) {
  if (line == nullptr) {
    return;
  }

  if (strcmp(line, "+MATREADY") == 0) {
    matreadySeen_ = true;
  }
}

bool StartupSequencerCore::isComplete() const {
  return state_ == State::Complete;
}

bool StartupSequencerCore::hasMatreadySeen() const {
  return matreadySeen_;
}

void StartupSequencerCore::advance(uint32_t nowMs) {
  attempts_ = 0;
  commandIndex_++;
  if (commandIndex_ >= StartupCommands::count()) {
    state_ = State::Complete;
    nextActionMs_ = nowMs;
    return;
  }

  nextActionMs_ = nowMs;
}

bool StartupSequencerCore::commandCanRun(uint32_t nowMs) const {
  if (pending_) {
    return false;
  }

  if (state_ != State::Command) {
    return false;
  }

  return static_cast<int32_t>(nowMs - nextActionMs_) >= 0;
}
