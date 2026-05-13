#pragma once

#include "sms_receiver.h"

#include <stddef.h>
#include <stdint.h>

using SmsPduDecodeFn = bool (*)(const char* pdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData);

class SmsReceiverCore {
 public:
  static constexpr uint32_t kPduWaitTimeoutMs = 5000;
  static constexpr uint32_t kConcatTimeoutMs = 30000;
  static constexpr uint8_t kMaxConcatParts = 10;
  static constexpr uint8_t kMaxConcatMessages = 5;
  static constexpr size_t kConcatPartTextCapacity = 384;

  void begin(SmsPduDecodeFn decoder, void* decoderUserData);
  bool onUrc(const char* line, uint32_t nowMs);
  bool processPdu(const char* pdu, uint32_t nowMs);
  void poll(uint32_t nowMs);

  void setReceivedCallback(SmsReceivedCallback callback, void* userData);
  void setDecodedCallback(SmsDecodedCallback callback, void* userData);
  void setErrorCallback(SmsErrorCallback callback, void* userData);
  void setStorageCallback(SmsStorageCallback callback, void* userData);

 private:
  struct ConcatSlot {
    bool inUse;
    uint8_t ref;
    uint8_t totalParts;
    uint8_t receivedParts;
    uint32_t firstPartMs;
    SmsMessage baseMessage;
    bool partValid[kMaxConcatParts];
    char partText[kMaxConcatParts][kConcatPartTextCapacity];
  };

  bool handlePduLine(const char* line, uint32_t nowMs);
  bool handleDecodedMessage(const SmsMessage& message, uint32_t nowMs, const char* rawPdu);
  bool handleConcatMessage(const SmsMessage& message, uint32_t nowMs, const char* rawPdu);
  int findConcatSlot(const SmsMessage& message) const;
  int findFreeConcatSlot() const;
  void clearConcatSlot(uint8_t slotIndex);
  void clearConcatSlots();
  bool emitConcatSlot(uint8_t slotIndex, bool complete);
  void emitError(const char* reason, const char* rawLine);
  void emitStorage(const SmsStorageNotification& notification);
  static bool parseCmtiLine(const char* line, SmsStorageNotification& notification);
  static bool startsWith(const char* value, const char* prefix);
  static bool isHexString(const char* value);
  static void copyText(char* dest, size_t destCapacity, const char* source);
  static bool appendText(char* dest, size_t destCapacity, const char* source);

  bool awaitingPdu_ = false;
  uint32_t pduDeadlineMs_ = 0;
  char cmtHeader_[64];

  SmsPduDecodeFn decoder_ = nullptr;
  void* decoderUserData_ = nullptr;

  SmsReceivedCallback receivedCallback_ = nullptr;
  void* receivedUserData_ = nullptr;

  SmsDecodedCallback decodedCallback_ = nullptr;
  void* decodedUserData_ = nullptr;

  SmsErrorCallback errorCallback_ = nullptr;
  void* errorUserData_ = nullptr;

  SmsStorageCallback storageCallback_ = nullptr;
  void* storageUserData_ = nullptr;

  ConcatSlot concatSlots_[kMaxConcatMessages];
};
