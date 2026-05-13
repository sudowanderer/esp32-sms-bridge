#pragma once

#include "modem_at.h"
#include "sms_receiver.h"

#include <stddef.h>
#include <stdint.h>

enum class SmsStorageReaderEvent {
  Enqueued,
  QueueFull,
  ReadCommandReady,
  ReadFailed,
  PduReady,
  DeleteCommandReady,
  DeleteSucceeded,
  DeleteFailed,
};

struct SmsStorageReaderStatus {
  SmsStorageReaderEvent event;
  char storage[8];
  uint16_t index;
  char detail[64];
};

using SmsStorageReaderStatusCallback = void (*)(const SmsStorageReaderStatus& status, void* userData);

class SmsStorageReaderCore {
 public:
  static constexpr uint8_t kCapacity = 4;

  void begin();
  void setStatusCallback(SmsStorageReaderStatusCallback callback, void* userData);

  bool enqueue(const SmsStorageNotification& notification);
  bool nextReadCommand(char* command, size_t commandCapacity);
  bool completeRead(ModemAtResult result, const char* response, char* pdu, size_t pduCapacity);
  bool messageQueued(char* deleteCommand, size_t deleteCommandCapacity);
  void messageRejected();
  void completeDelete(ModemAtResult result);

  bool isActive() const;
  uint8_t queuedCount() const;

 private:
  struct StoredSms {
    char storage[8];
    uint16_t index;
  };

  enum class ActiveState {
    None,
    Reading,
    AwaitingMessageQueue,
    Deleting,
  };

  static void copyText(char* dest, size_t destCapacity, const char* source);
  static bool extractPduLine(const char* response, char* pdu, size_t pduCapacity);
  void emit(SmsStorageReaderEvent event, const char* detail);
  void clearActive();
  bool hasQueuedIndex(const SmsStorageNotification& notification) const;

  StoredSms queue_[kCapacity];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
  StoredSms active_ = {};
  ActiveState activeState_ = ActiveState::None;
  SmsStorageReaderStatusCallback statusCallback_ = nullptr;
  void* statusUserData_ = nullptr;
};
