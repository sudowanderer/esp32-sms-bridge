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

bool extractApn(const char* line, char* output, size_t outputSize) {
  if (line == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }

  output[0] = '\0';
  const char* cursor = line;
  for (uint8_t quote = 0; quote < 3; ++quote) {
    cursor = strchr(cursor, '"');
    if (cursor == nullptr) {
      return false;
    }
    ++cursor;
  }

  const char* end = strchr(cursor, '"');
  if (end == nullptr) {
    return false;
  }

  const size_t len = static_cast<size_t>(end - cursor);
  if (len >= outputSize) {
    return false;
  }

  memcpy(output, cursor, len);
  output[len] = '\0';
  return true;
}

bool equalsIgnoreCase(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }

  while (*a != '\0' && *b != '\0') {
    char ca = *a;
    char cb = *b;
    if (ca >= 'a' && ca <= 'z') {
      ca = static_cast<char>(ca - 'a' + 'A');
    }
    if (cb >= 'a' && cb <= 'z') {
      cb = static_cast<char>(cb - 'a' + 'A');
    }
    if (ca != cb) {
      return false;
    }
    ++a;
    ++b;
  }

  return *a == '\0' && *b == '\0';
}

}  // namespace

void PdpGuardCore::begin() {
  startupComplete_ = false;
  pending_ = false;
  deactivated_ = false;
  onlyIgnoredContextsActive_ = false;
  nextAttemptMs_ = 0;
  state_ = State::QueryContexts;
  resetContexts();
  targetCid_ = 0;
  command_[0] = '\0';
}

void PdpGuardCore::setStartupComplete(bool complete, uint32_t nowMs) {
  if (startupComplete_ == complete) {
    return;
  }

  startupComplete_ = complete;
  if (complete && !deactivated_) {
    state_ = State::QueryContexts;
    nextAttemptMs_ = nowMs;
  }
}

const char* PdpGuardCore::commandToSubmit(uint32_t nowMs) {
  if (!startupComplete_ || pending_ || deactivated_ || onlyIgnoredContextsActive_) {
    return nullptr;
  }

  if (static_cast<int32_t>(nowMs - nextAttemptMs_) < 0) {
    return nullptr;
  }

  switch (state_) {
    case State::QueryContexts:
      return ModemCommands::queryPdpContext();
    case State::QueryActivation:
      return ModemCommands::queryPdpActivation();
    case State::Deactivate:
      if (!ModemCommands::buildDeactivatePdpContext(targetCid_, command_, sizeof(command_))) {
        return nullptr;
      }
      return command_;
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

  if (state_ == State::QueryContexts) {
    if (!parseContexts(response)) {
      scheduleRetry(nowMs);
      return;
    }
    state_ = State::QueryActivation;
    nextAttemptMs_ = nowMs;
    return;
  }

  if (state_ == State::QueryActivation) {
    if (!parseActivation(response)) {
      scheduleRetry(nowMs);
      return;
    }

    const int target = findTargetIndex();
    if (target < 0) {
      deactivated_ = true;
      state_ = State::Complete;
      return;
    }

    targetCid_ = contexts_[target].cid;
    state_ = State::Deactivate;
    nextAttemptMs_ = nowMs;
    return;
  }

  if (state_ == State::Deactivate) {
    state_ = State::QueryContexts;
    resetContexts();
    nextAttemptMs_ = nowMs;
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

bool PdpGuardCore::hasOnlyIgnoredContextsActive() const {
  return onlyIgnoredContextsActive_;
}

uint8_t PdpGuardCore::lastTargetCid() const {
  return targetCid_;
}

void PdpGuardCore::scheduleRetry(uint32_t nowMs) {
  state_ = State::QueryContexts;
  pending_ = false;
  nextAttemptMs_ = nowMs + kRetryIntervalMs;
  resetContexts();
}

void PdpGuardCore::resetContexts() {
  memset(contexts_, 0, sizeof(contexts_));
  contextCount_ = 0;
  targetCid_ = 0;
}

bool PdpGuardCore::parseContexts(const char* response) {
  resetContexts();
  const char* cursor = response;
  bool found = false;
  while ((cursor = findLine(cursor, "+CGDCONT:")) != nullptr) {
    unsigned int cid = 0;
    if (sscanf(cursor, "+CGDCONT: %u,", &cid) == 1 && cid <= 255) {
      char apn[32];
      if (!extractApn(cursor, apn, sizeof(apn))) {
        return false;
      }
      Context* context = findOrAddContext(static_cast<uint8_t>(cid));
      if (context == nullptr) {
        return false;
      }
      context->known = true;
      context->ignored = equalsIgnoreCase(apn, "IMS");
      found = true;
    }

    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }
  return found;
}

bool PdpGuardCore::parseActivation(const char* response) {
  for (uint8_t i = 0; i < contextCount_; ++i) {
    contexts_[i].active = false;
  }

  const char* cursor = response;
  bool found = false;
  while ((cursor = findLine(cursor, "+CGACT:")) != nullptr) {
    unsigned int cid = 0;
    unsigned int state = 0;
    if (sscanf(cursor, "+CGACT: %u,%u", &cid, &state) == 2 && cid <= 255) {
      for (uint8_t i = 0; i < contextCount_; ++i) {
        if (contexts_[i].cid == static_cast<uint8_t>(cid)) {
          contexts_[i].active = state == 1;
          break;
        }
      }
      found = true;
    }

    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }
  return found;
}

int PdpGuardCore::findTargetIndex() {
  bool anyActive = false;
  bool anyTarget = false;
  int target = -1;

  for (uint8_t i = 0; i < contextCount_; ++i) {
    if (!contexts_[i].active) {
      continue;
    }

    anyActive = true;
    if (!contexts_[i].ignored) {
      anyTarget = true;
      target = i;
      break;
    }
  }

  onlyIgnoredContextsActive_ = anyActive && !anyTarget;
  return target;
}

PdpGuardCore::Context* PdpGuardCore::findOrAddContext(uint8_t cid) {
  for (uint8_t i = 0; i < contextCount_; ++i) {
    if (contexts_[i].cid == cid) {
      return &contexts_[i];
    }
  }

  if (contextCount_ >= kMaxContexts) {
    return nullptr;
  }

  Context* context = &contexts_[contextCount_++];
  context->cid = cid;
  context->known = false;
  context->active = false;
  context->ignored = false;
  return context;
}
