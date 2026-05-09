#include "sms_queue.h"

static SmsQueueCore smsQueueCore;

void smsQueueBegin() {
  smsQueueCore.begin();
}

bool smsQueueEnqueue(const SmsMessage& message, uint32_t nowMs) {
  return smsQueueCore.enqueue(message, nowMs);
}

SmsQueueItem* smsQueueAcquireNext(uint32_t nowMs) {
  return smsQueueCore.acquireNext(nowMs);
}

void smsQueueMarkSending(SmsQueueItem* item, uint32_t nowMs) {
  smsQueueCore.markSending(item, nowMs);
}

void smsQueueMarkSent(SmsQueueItem* item, uint32_t nowMs) {
  smsQueueCore.markSent(item, nowMs);
}

void smsQueueMarkFailed(SmsQueueItem* item, const char* error, uint32_t retryDelayMs, uint32_t nowMs) {
  smsQueueCore.markFailed(item, error, retryDelayMs, nowMs);
}

uint8_t smsQueueDepth() {
  return smsQueueCore.depth();
}

uint8_t smsQueuePendingCount() {
  return smsQueueCore.pendingCount();
}

uint8_t smsQueueCapacity() {
  return smsQueueCore.capacity();
}

const SmsQueueItem* smsQueueGet(uint8_t index) {
  return smsQueueCore.get(index);
}
