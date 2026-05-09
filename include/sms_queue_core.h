#pragma once

#include "sms_receiver.h"

#include <stdint.h>

enum class SmsQueueStatus {
  Pending,
  Sending,
  Sent,
  Failed,
};

struct SmsQueueItem {
  SmsMessage message;
  SmsQueueStatus status;
  uint8_t attemptCount;
  uint32_t nextAttemptMs;
  uint32_t createdAtMs;
  uint32_t updatedAtMs;
  char lastError[96];
};

class SmsQueueCore {
 public:
  static constexpr uint8_t kCapacity = 8;

  void begin();

  bool enqueue(const SmsMessage& message, uint32_t nowMs);
  SmsQueueItem* acquireNext(uint32_t nowMs);

  void markSending(SmsQueueItem* item, uint32_t nowMs);
  void markSent(SmsQueueItem* item, uint32_t nowMs);
  void markFailed(SmsQueueItem* item, const char* error, uint32_t retryDelayMs, uint32_t nowMs);

  uint8_t depth() const;
  uint8_t pendingCount() const;
  uint8_t capacity() const;

  const SmsQueueItem* get(uint8_t index) const;

 private:
  SmsQueueItem items_[kCapacity];
  uint8_t count_ = 0;
};
