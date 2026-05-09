#include "sms_receiver_core.h"

#include <string.h>

void SmsReceiverCore::begin(SmsPduDecodeFn decoder, void* decoderUserData) {
  decoder_ = decoder;
  decoderUserData_ = decoderUserData;
  awaitingPdu_ = false;
  pduDeadlineMs_ = 0;
  cmtHeader_[0] = '\0';
}

bool SmsReceiverCore::onUrc(const char* line, uint32_t nowMs) {
  if (line == nullptr || line[0] == '\0') {
    return false;
  }

  if (awaitingPdu_) {
    handlePduLine(line);
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
  if (!awaitingPdu_) {
    return;
  }

  if (static_cast<int32_t>(nowMs - pduDeadlineMs_) < 0) {
    return;
  }

  emitError("pdu_timeout", cmtHeader_);
  awaitingPdu_ = false;
}

void SmsReceiverCore::setReceivedCallback(SmsReceivedCallback callback, void* userData) {
  receivedCallback_ = callback;
  receivedUserData_ = userData;
}

void SmsReceiverCore::setErrorCallback(SmsErrorCallback callback, void* userData) {
  errorCallback_ = callback;
  errorUserData_ = userData;
}

void SmsReceiverCore::handlePduLine(const char* line) {
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

  if (receivedCallback_ != nullptr) {
    receivedCallback_(message, receivedUserData_);
  }
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
