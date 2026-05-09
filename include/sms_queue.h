#pragma once

#include "sms_queue_core.h"

#include <stdint.h>

void smsQueueBegin();

bool smsQueueEnqueue(const SmsMessage& message, uint32_t nowMs);
SmsQueueItem* smsQueueAcquireNext(uint32_t nowMs);

void smsQueueMarkSending(SmsQueueItem* item, uint32_t nowMs);
void smsQueueMarkSent(SmsQueueItem* item, uint32_t nowMs);
void smsQueueMarkFailed(SmsQueueItem* item, const char* error, uint32_t retryDelayMs, uint32_t nowMs);

uint8_t smsQueueDepth();
uint8_t smsQueuePendingCount();
uint8_t smsQueueCapacity();

const SmsQueueItem* smsQueueGet(uint8_t index);
