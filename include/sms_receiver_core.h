#pragma once

#include "sms_receiver.h"

#include <stddef.h>
#include <stdint.h>

using SmsPduDecodeFn = bool (*)(const char* pdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData);

class SmsReceiverCore {
 public:
  static constexpr uint32_t kPduWaitTimeoutMs = 5000;

  void begin(SmsPduDecodeFn decoder, void* decoderUserData);
  bool onUrc(const char* line, uint32_t nowMs);
  void poll(uint32_t nowMs);

  void setReceivedCallback(SmsReceivedCallback callback, void* userData);
  void setErrorCallback(SmsErrorCallback callback, void* userData);

 private:
  void handlePduLine(const char* line);
  void emitError(const char* reason, const char* rawLine);
  static bool startsWith(const char* value, const char* prefix);
  static bool isHexString(const char* value);
  static void copyText(char* dest, size_t destCapacity, const char* source);

  bool awaitingPdu_ = false;
  uint32_t pduDeadlineMs_ = 0;
  char cmtHeader_[64];

  SmsPduDecodeFn decoder_ = nullptr;
  void* decoderUserData_ = nullptr;

  SmsReceivedCallback receivedCallback_ = nullptr;
  void* receivedUserData_ = nullptr;

  SmsErrorCallback errorCallback_ = nullptr;
  void* errorUserData_ = nullptr;
};
