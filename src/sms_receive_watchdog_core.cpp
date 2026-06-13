#include "sms_receive_watchdog_core.h"

#include "modem_commands.h"

#include <stdio.h>
#include <string.h>

void SmsReceiveWatchdogCore::begin(uint32_t nowMs) {
  startupComplete_ = false;
  pending_ = false;
  step_ = Step::PduMode;
  work_ = Work::None;
  nextActionMs_ = nowMs;
  nextConfigMs_ = nowMs;
  nextStatusQueryMs_ = nowMs;
  lastConfigMs_ = 0;
  recoveryCount_ = 0;
  snapshot_ = {};
}

void SmsReceiveWatchdogCore::setStartupComplete(bool complete, uint32_t nowMs) {
  const bool wasComplete = startupComplete_;
  startupComplete_ = complete;
  if (complete && !wasComplete) {
    requestRecovery(nowMs);
  }
}

void SmsReceiveWatchdogCore::onUrc(const char* line, uint32_t nowMs) {
  if (!startupComplete_ || line == nullptr) {
    return;
  }

  if (strcmp(line, "+MATREADY") == 0 || strcmp(line, "+CPIN: READY") == 0) {
    requestRecovery(nowMs);
  }
}

const char* SmsReceiveWatchdogCore::commandToSubmit(uint32_t nowMs) const {
  if (!startupComplete_ || pending_ || !due(nowMs)) {
    return nullptr;
  }

  if (work_ == Work::None) {
    if (static_cast<int32_t>(nowMs - nextConfigMs_) >= 0) {
      return ModemCommands::smsPduMode();
    }
    if (static_cast<int32_t>(nowMs - nextStatusQueryMs_) >= 0) {
      return ModemCommands::querySmsUrcMode();
    }
    return nullptr;
  }

  if (work_ == Work::Configure) {
    switch (step_) {
      case Step::PduMode:
        return ModemCommands::smsPduMode();
      case Step::DirectUrcMode:
        return ModemCommands::smsDirectUrcMode();
      case Step::QueryUrcMode:
        return ModemCommands::querySmsUrcMode();
    }
  }

  if (work_ == Work::QueryOnly) {
    return ModemCommands::querySmsUrcMode();
  }

  return nullptr;
}

void SmsReceiveWatchdogCore::markSubmitted(uint32_t nowMs) {
  if (work_ == Work::None) {
    if (static_cast<int32_t>(nowMs - nextConfigMs_) >= 0) {
      work_ = Work::Configure;
      step_ = Step::PduMode;
    } else {
      work_ = Work::QueryOnly;
      step_ = Step::QueryUrcMode;
    }
  }

  pending_ = true;
}

void SmsReceiveWatchdogCore::deferSubmission(uint32_t nowMs) {
  if (!pending_) {
    nextActionMs_ = nowMs + kRetryDelayMs;
  }
}

void SmsReceiveWatchdogCore::complete(ModemAtResult result, uint32_t nowMs) {
  complete(result, nullptr, nowMs);
}

void SmsReceiveWatchdogCore::complete(ModemAtResult result, const char* response, uint32_t nowMs) {
  if (!pending_) {
    return;
  }

  pending_ = false;
  if (result != ModemAtResult::Ok) {
    if (work_ == Work::QueryOnly || step_ == Step::QueryUrcMode) {
      work_ = Work::None;
      step_ = Step::PduMode;
      nextStatusQueryMs_ = nowMs + kStatusQueryMs;
      nextActionMs_ = static_cast<int32_t>(nextConfigMs_ - nextStatusQueryMs_) < 0 ? nextConfigMs_ : nextStatusQueryMs_;
      return;
    }

    step_ = Step::PduMode;
    nextActionMs_ = nowMs + kRetryDelayMs;
    nextConfigMs_ = nextActionMs_;
    return;
  }

  if (work_ == Work::QueryOnly) {
    (void)parseCnmiResponse(response, snapshot_, nowMs);
    work_ = Work::None;
    step_ = Step::PduMode;
    nextStatusQueryMs_ = nowMs + kStatusQueryMs;
    nextActionMs_ = static_cast<int32_t>(nextConfigMs_ - nextStatusQueryMs_) < 0 ? nextConfigMs_ : nextStatusQueryMs_;
    return;
  }

  if (step_ == Step::PduMode) {
    step_ = Step::DirectUrcMode;
    nextActionMs_ = nowMs;
    return;
  }

  if (step_ == Step::DirectUrcMode) {
    step_ = Step::QueryUrcMode;
    lastConfigMs_ = nowMs;
    recoveryCount_++;
    nextConfigMs_ = nowMs + kConfigRefreshMs;
    nextActionMs_ = nowMs;
    return;
  }

  (void)parseCnmiResponse(response, snapshot_, nowMs);
  work_ = Work::None;
  step_ = Step::PduMode;
  nextStatusQueryMs_ = nowMs + kStatusQueryMs;
  nextActionMs_ = static_cast<int32_t>(nextConfigMs_ - nextStatusQueryMs_) < 0 ? nextConfigMs_ : nextStatusQueryMs_;
}

bool SmsReceiveWatchdogCore::pending() const {
  return pending_;
}

uint32_t SmsReceiveWatchdogCore::lastConfigMs() const {
  return lastConfigMs_;
}

uint16_t SmsReceiveWatchdogCore::recoveryCount() const {
  return recoveryCount_;
}

SmsReceiveWatchdogSnapshot SmsReceiveWatchdogCore::snapshot() const {
  return snapshot_;
}

const char* SmsReceiveWatchdogCore::modeDescription(uint8_t value) {
  switch (value) {
    case 0:
      return "buffer unsolicited messages";
    case 1:
      return "discard while busy";
    case 2:
      return "buffer while busy";
    case 3:
      return "forward while busy";
  }
  return "unknown mode";
}

const char* SmsReceiveWatchdogCore::mtDescription(uint8_t value) {
  switch (value) {
    case 0:
      return "no SMS-DELIVER indication";
    case 1:
      return "store SMS and report +CMTI";
    case 2:
      return "direct SMS PDU via +CMT";
    case 3:
      return "direct class 3 SMS";
  }
  return "unknown SMS routing";
}

const char* SmsReceiveWatchdogCore::bmDescription(uint8_t value) {
  switch (value) {
    case 0:
      return "no cell broadcast";
    case 2:
      return "direct cell broadcast";
  }
  return "cell broadcast setting";
}

const char* SmsReceiveWatchdogCore::dsDescription(uint8_t value) {
  switch (value) {
    case 0:
      return "no status report";
    case 1:
      return "status report via +CDS";
    case 2:
      return "status report stored";
  }
  return "status report setting";
}

const char* SmsReceiveWatchdogCore::bfrDescription(uint8_t value) {
  switch (value) {
    case 0:
      return "flush buffered messages";
    case 1:
      return "clear buffered messages";
  }
  return "buffer handling setting";
}

bool SmsReceiveWatchdogCore::parseCnmiResponse(const char* response,
                                               SmsReceiveWatchdogSnapshot& snapshot,
                                               uint32_t nowMs) {
  if (response == nullptr) {
    return false;
  }

  const char* line = response;
  while (*line != '\0') {
    while (*line == '\n' || *line == '\r') {
      line++;
    }

    if (startsWith(line, "+CNMI:")) {
      const char* cursor = line + strlen("+CNMI:");
      while (*cursor == ' ') {
        cursor++;
      }

      uint8_t mode = 0;
      uint8_t mt = 0;
      uint8_t bm = 0;
      uint8_t ds = 0;
      uint8_t bfr = 0;
      if (!parseUint8(cursor, mode) || *cursor++ != ',' || !parseUint8(cursor, mt) || *cursor++ != ',' ||
          !parseUint8(cursor, bm) || *cursor++ != ',' || !parseUint8(cursor, ds) || *cursor++ != ',' ||
          !parseUint8(cursor, bfr)) {
        return false;
      }

      snapshot.cnmiKnown = true;
      snapshot.cnmiMode = mode;
      snapshot.cnmiMt = mt;
      snapshot.cnmiBm = bm;
      snapshot.cnmiDs = ds;
      snapshot.cnmiBfr = bfr;
      snapshot.cnmiLastUpdatedMs = nowMs;
      snprintf(snapshot.cnmiRaw, sizeof(snapshot.cnmiRaw), "%u,%u,%u,%u,%u", mode, mt, bm, ds, bfr);
      snprintf(snapshot.cnmiDescription,
               sizeof(snapshot.cnmiDescription),
               "mode %u: %s; mt %u: %s; bm %u: %s; ds %u: %s; bfr %u: %s",
               mode,
               modeDescription(mode),
               mt,
               mtDescription(mt),
               bm,
               bmDescription(bm),
               ds,
               dsDescription(ds),
               bfr,
               bfrDescription(bfr));
      return true;
    }

    const char* next = strchr(line, '\n');
    if (next == nullptr) {
      break;
    }
    line = next + 1;
  }

  return false;
}

bool SmsReceiveWatchdogCore::parseUint8(const char*& cursor, uint8_t& value) {
  if (cursor == nullptr || *cursor < '0' || *cursor > '9') {
    return false;
  }

  uint16_t parsed = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    parsed = static_cast<uint16_t>((parsed * 10) + static_cast<uint16_t>(*cursor - '0'));
    if (parsed > 255) {
      return false;
    }
    cursor++;
  }

  value = static_cast<uint8_t>(parsed);
  return true;
}

void SmsReceiveWatchdogCore::copyText(char* dest, size_t destCapacity, const char* source) {
  if (dest == nullptr || destCapacity == 0) {
    return;
  }

  if (source == nullptr) {
    dest[0] = '\0';
    return;
  }

  strncpy(dest, source, destCapacity - 1);
  dest[destCapacity - 1] = '\0';
}

bool SmsReceiveWatchdogCore::startsWith(const char* value, const char* prefix) {
  return value != nullptr && prefix != nullptr && strncmp(value, prefix, strlen(prefix)) == 0;
}

bool SmsReceiveWatchdogCore::due(uint32_t nowMs) const {
  return static_cast<int32_t>(nowMs - nextActionMs_) >= 0;
}

void SmsReceiveWatchdogCore::requestRecovery(uint32_t nowMs) {
  if (pending_) {
    nextConfigMs_ = nowMs;
    return;
  }

  work_ = Work::Configure;
  step_ = Step::PduMode;
  nextConfigMs_ = nowMs;
  nextActionMs_ = nowMs;
}
