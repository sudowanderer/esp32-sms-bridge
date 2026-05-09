#include "sms_queue_core.h"

#include <string.h>

void SmsQueueCore::begin() {
  memset(items_, 0, sizeof(items_));
  count_ = 0;
}

bool SmsQueueCore::enqueue(const SmsMessage& message, uint32_t nowMs) {
  if (count_ >= kCapacity) {
    return false;
  }

  SmsQueueItem& item = items_[count_];
  memset(&item, 0, sizeof(item));
  item.message = message;
  item.status = SmsQueueStatus::Pending;
  item.attemptCount = 0;
  item.nextAttemptMs = nowMs;
  item.createdAtMs = nowMs;
  item.updatedAtMs = nowMs;
  item.lastError[0] = '\0';
  count_++;
  return true;
}

SmsQueueItem* SmsQueueCore::acquireNext(uint32_t nowMs) {
  for (uint8_t i = 0; i < count_; ++i) {
    SmsQueueItem& item = items_[i];
    const bool retryDue = static_cast<int32_t>(nowMs - item.nextAttemptMs) >= 0;
    if ((item.status == SmsQueueStatus::Pending || item.status == SmsQueueStatus::Failed) && retryDue) {
      return &item;
    }
  }

  return nullptr;
}

void SmsQueueCore::markSending(SmsQueueItem* item, uint32_t nowMs) {
  if (item == nullptr) {
    return;
  }

  item->status = SmsQueueStatus::Sending;
  item->updatedAtMs = nowMs;
}

void SmsQueueCore::markSent(SmsQueueItem* item, uint32_t nowMs) {
  if (item == nullptr) {
    return;
  }

  item->status = SmsQueueStatus::Sent;
  item->updatedAtMs = nowMs;
}

void SmsQueueCore::markFailed(SmsQueueItem* item, const char* error, uint32_t retryDelayMs, uint32_t nowMs) {
  if (item == nullptr) {
    return;
  }

  item->status = SmsQueueStatus::Failed;
  if (item->attemptCount < 255) {
    item->attemptCount++;
  }
  item->nextAttemptMs = nowMs + retryDelayMs;
  item->updatedAtMs = nowMs;

  if (error == nullptr) {
    item->lastError[0] = '\0';
    return;
  }

  strncpy(item->lastError, error, sizeof(item->lastError) - 1);
  item->lastError[sizeof(item->lastError) - 1] = '\0';
}

uint8_t SmsQueueCore::depth() const {
  return count_;
}

uint8_t SmsQueueCore::pendingCount() const {
  uint8_t pending = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    if (items_[i].status == SmsQueueStatus::Pending || items_[i].status == SmsQueueStatus::Failed) {
      pending++;
    }
  }

  return pending;
}

uint8_t SmsQueueCore::capacity() const {
  return kCapacity;
}

const SmsQueueItem* SmsQueueCore::get(uint8_t index) const {
  if (index >= count_) {
    return nullptr;
  }

  return &items_[index];
}
