#include "pdp_guard_core.h"

#include "modem_commands.h"

#include <stdio.h>
#include <string.h>

namespace {

const char* findLine(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) {
    return nullptr;
  }

  const size_t prefixLen = strlen(prefix);
  const char* cursor = text;
  while (*cursor != '\0') {
    while (*cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (strncmp(cursor, prefix, prefixLen) == 0) {
      return cursor;
    }
    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }
  return nullptr;
}

}  // namespace

void PdpGuardCore::begin() {
  startupComplete_ = false;
  pending_ = false;
  deactivated_ = false;
  alreadyDisconnected_ = false;
  nextAttemptMs_ = 0;
  state_ = State::QueryConnection;
}

void PdpGuardCore::setStartupComplete(bool complete, uint32_t nowMs) {
  if (startupComplete_ == complete) {
    return;
  }

  startupComplete_ = complete;
  if (complete && !deactivated_) {
    state_ = State::QueryConnection;
    nextAttemptMs_ = nowMs;
  }
}

const char* PdpGuardCore::commandToSubmit(uint32_t nowMs) {
  if (!startupComplete_ || pending_ || deactivated_) {
    return nullptr;
  }

  if (static_cast<int32_t>(nowMs - nextAttemptMs_) < 0) {
    return nullptr;
  }

  switch (state_) {
    case State::QueryConnection:
      return ModemCommands::queryMipCall();
    case State::Disconnect:
      return ModemCommands::disconnectMipCall();
    case State::Complete:
      return nullptr;
  }

  return nullptr;
}

void PdpGuardCore::markSubmitted() {
  pending_ = true;
}

void PdpGuardCore::complete(ModemAtResult result, const char* response, uint32_t nowMs) {
  if (!pending_) {
    return;
  }

  pending_ = false;
  if (result != ModemAtResult::Ok) {
    scheduleRetry(nowMs);
    return;
  }

  if (state_ == State::QueryConnection) {
    bool active = false;
    if (!parseConnectionActive(response, active)) {
      scheduleRetry(nowMs);
      return;
    }

    if (!active) {
      alreadyDisconnected_ = true;
      deactivated_ = true;
      state_ = State::Complete;
      return;
    }

    state_ = State::Disconnect;
    nextAttemptMs_ = nowMs;
    return;
  }

  if (state_ == State::Disconnect) {
    deactivated_ = true;
    state_ = State::Complete;
  }
}

void PdpGuardCore::deferSubmission(uint32_t nowMs) {
  if (!pending_) {
    nextAttemptMs_ = nowMs + kRetryIntervalMs;
  }
}

bool PdpGuardCore::isPending() const {
  return pending_;
}

bool PdpGuardCore::isDeactivated() const {
  return deactivated_;
}

bool PdpGuardCore::isAlreadyDisconnected() const {
  return alreadyDisconnected_;
}

void PdpGuardCore::scheduleRetry(uint32_t nowMs) {
  state_ = State::QueryConnection;
  pending_ = false;
  nextAttemptMs_ = nowMs + kRetryIntervalMs;
}

bool PdpGuardCore::parseConnectionActive(const char* response, bool& active) const {
  const char* line = findLine(response, "+MIPCALL:");
  if (line == nullptr) {
    return false;
  }

  unsigned int cid = 0;
  unsigned int state = 0;
  if (sscanf(line, "+MIPCALL: %u,%u", &cid, &state) != 2) {
    return false;
  }

  active = state == 1;
  return true;
}
