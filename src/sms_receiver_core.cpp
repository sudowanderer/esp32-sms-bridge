#include "sms_receiver_core.h"

#include <stdio.h>
#include <string.h>

void SmsReceiverCore::begin(SmsPduDecodeFn decoder, void* decoderUserData) {
  decoder_ = decoder;
  decoderUserData_ = decoderUserData;
  awaitingPdu_ = false;
  pduDeadlineMs_ = 0;
  cmtHeader_[0] = '\0';
  clearConcatSlots();
}

bool SmsReceiverCore::onUrc(const char* line, uint32_t nowMs) {
  if (line == nullptr || line[0] == '\0') {
    return false;
  }

  if (awaitingPdu_) {
    handlePduLine(line, nowMs);
    return true;
  }

  if (!startsWith(line, "+CMT:")) {
    return false;
  }

  // +CMT 是短信上报头，真实短信内容在下一行 PDU；这里先进入等待状态。
  awaitingPdu_ = true;
  pduDeadlineMs_ = nowMs + kPduWaitTimeoutMs;
  copyText(cmtHeader_, sizeof(cmtHeader_), line);
  return true;
}

void SmsReceiverCore::poll(uint32_t nowMs) {
  if (awaitingPdu_ && static_cast<int32_t>(nowMs - pduDeadlineMs_) >= 0) {
    emitError("pdu_timeout", cmtHeader_);
    awaitingPdu_ = false;
  }

  for (uint8_t i = 0; i < kMaxConcatMessages; ++i) {
    if (!concatSlots_[i].inUse) {
      continue;
    }

    if (static_cast<int32_t>(nowMs - (concatSlots_[i].firstPartMs + kConcatTimeoutMs)) >= 0) {
      emitError("concat_timeout", concatSlots_[i].baseMessage.pdu);
      emitConcatSlot(i, false);
    }
  }
}

void SmsReceiverCore::setReceivedCallback(SmsReceivedCallback callback, void* userData) {
  receivedCallback_ = callback;
  receivedUserData_ = userData;
}

void SmsReceiverCore::setErrorCallback(SmsErrorCallback callback, void* userData) {
  errorCallback_ = callback;
  errorUserData_ = userData;
}

void SmsReceiverCore::handlePduLine(const char* line, uint32_t nowMs) {
  awaitingPdu_ = false;

  if (!isHexString(line)) {
    emitError("pdu_not_hex", line);
    return;
  }

  if (decoder_ == nullptr) {
    emitError("pdu_decoder_missing", line);
    return;
  }

  SmsMessage message = {};
  char error[48] = {};
  if (!decoder_(line, &message, error, sizeof(error), decoderUserData_)) {
    emitError(error[0] != '\0' ? error : "pdu_decode_failed", line);
    return;
  }

  copyText(message.pdu, sizeof(message.pdu), line);

  handleDecodedMessage(message, nowMs, line);
}

void SmsReceiverCore::handleDecodedMessage(const SmsMessage& message, uint32_t nowMs, const char* rawPdu) {
  if (message.isConcat && message.concatTotal > 1) {
    handleConcatMessage(message, nowMs, rawPdu);
    return;
  }

  if (receivedCallback_ != nullptr) {
    receivedCallback_(message, receivedUserData_);
  }
}

void SmsReceiverCore::handleConcatMessage(const SmsMessage& message, uint32_t nowMs, const char* rawPdu) {
  if (message.concatPart == 0 || message.concatTotal == 0 || message.concatPart > message.concatTotal ||
      message.concatTotal > kMaxConcatParts) {
    emitError("concat_invalid_part", rawPdu);
    return;
  }

  int slotIndex = findConcatSlot(message);
  if (slotIndex < 0) {
    slotIndex = findFreeConcatSlot();
    if (slotIndex < 0) {
      emitError("concat_cache_full", rawPdu);
      return;
    }

    ConcatSlot& slot = concatSlots_[slotIndex];
    slot.inUse = true;
    slot.ref = message.concatRef;
    slot.totalParts = message.concatTotal;
    slot.receivedParts = 0;
    slot.firstPartMs = nowMs;
    slot.baseMessage = message;
    for (uint8_t i = 0; i < kMaxConcatParts; ++i) {
      slot.partValid[i] = false;
      slot.partText[i][0] = '\0';
    }
  }

  ConcatSlot& slot = concatSlots_[slotIndex];
  const uint8_t partIndex = static_cast<uint8_t>(message.concatPart - 1);
  if (slot.partValid[partIndex]) {
    return;
  }

  slot.partValid[partIndex] = true;
  copyText(slot.partText[partIndex], sizeof(slot.partText[partIndex]), message.text);
  slot.receivedParts++;

  if (slot.receivedParts >= slot.totalParts) {
    emitConcatSlot(static_cast<uint8_t>(slotIndex), true);
  }
}

int SmsReceiverCore::findConcatSlot(const SmsMessage& message) const {
  for (uint8_t i = 0; i < kMaxConcatMessages; ++i) {
    const ConcatSlot& slot = concatSlots_[i];
    if (slot.inUse && slot.ref == message.concatRef && slot.totalParts == message.concatTotal &&
        strcmp(slot.baseMessage.sender, message.sender) == 0) {
      return i;
    }
  }

  return -1;
}

int SmsReceiverCore::findFreeConcatSlot() const {
  for (uint8_t i = 0; i < kMaxConcatMessages; ++i) {
    if (!concatSlots_[i].inUse) {
      return i;
    }
  }

  return -1;
}

void SmsReceiverCore::clearConcatSlot(uint8_t slotIndex) {
  if (slotIndex >= kMaxConcatMessages) {
    return;
  }

  ConcatSlot& slot = concatSlots_[slotIndex];
  memset(&slot, 0, sizeof(slot));
}

void SmsReceiverCore::clearConcatSlots() {
  for (uint8_t i = 0; i < kMaxConcatMessages; ++i) {
    clearConcatSlot(i);
  }
}

void SmsReceiverCore::emitConcatSlot(uint8_t slotIndex, bool complete) {
  if (slotIndex >= kMaxConcatMessages || !concatSlots_[slotIndex].inUse) {
    return;
  }

  ConcatSlot& slot = concatSlots_[slotIndex];
  SmsMessage merged = slot.baseMessage;
  merged.text[0] = '\0';
  merged.isConcat = true;
  merged.concatComplete = complete;
  merged.concatPartial = !complete;
  merged.concatPart = slot.totalParts;
  merged.concatTotal = slot.totalParts;
  merged.concatRef = slot.ref;

  bool truncated = false;
  for (uint8_t i = 0; i < slot.totalParts; ++i) {
    if (slot.partValid[i]) {
      if (!appendText(merged.text, sizeof(merged.text), slot.partText[i])) {
        truncated = true;
      }
      continue;
    }

    char missing[32];
    snprintf(missing, sizeof(missing), "[缺失分段%u]", static_cast<unsigned>(i + 1));
    if (!appendText(merged.text, sizeof(merged.text), missing)) {
      truncated = true;
    }
  }

  if (truncated) {
    emitError("concat_text_truncated", slot.baseMessage.pdu);
  }

  if (receivedCallback_ != nullptr) {
    receivedCallback_(merged, receivedUserData_);
  }

  clearConcatSlot(slotIndex);
}

void SmsReceiverCore::emitError(const char* reason, const char* rawLine) {
  if (errorCallback_ != nullptr) {
    errorCallback_(reason, rawLine, errorUserData_);
  }
}

bool SmsReceiverCore::startsWith(const char* value, const char* prefix) {
  return strncmp(value, prefix, strlen(prefix)) == 0;
}

bool SmsReceiverCore::isHexString(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }

  size_t len = 0;
  for (const char* p = value; *p != '\0'; ++p) {
    len++;
    const char c = *p;
    const bool isDigit = c >= '0' && c <= '9';
    const bool isUpperHex = c >= 'A' && c <= 'F';
    const bool isLowerHex = c >= 'a' && c <= 'f';
    if (!isDigit && !isUpperHex && !isLowerHex) {
      return false;
    }
  }

  return (len % 2) == 0;
}

void SmsReceiverCore::copyText(char* dest, size_t destCapacity, const char* source) {
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

bool SmsReceiverCore::appendText(char* dest, size_t destCapacity, const char* source) {
  if (dest == nullptr || destCapacity == 0) {
    return false;
  }

  if (source == nullptr || source[0] == '\0') {
    return true;
  }

  const size_t used = strlen(dest);
  if (used >= destCapacity - 1) {
    return false;
  }

  const size_t available = destCapacity - used - 1;
  const size_t sourceLen = strlen(source);
  const size_t copyLen = sourceLen < available ? sourceLen : available;
  memcpy(dest + used, source, copyLen);
  dest[used + copyLen] = '\0';
  return sourceLen <= available;
}
