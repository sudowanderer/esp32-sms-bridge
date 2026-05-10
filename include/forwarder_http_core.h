#pragma once

#include "sms_queue_core.h"

#include <stdint.h>

enum class PushHttpMethod {
  Post,
};

struct BarkChannelConfig {
  char serverUrl[128];
  char deviceKey[96];
};

struct PushChannelConfig {
  BarkChannelConfig bark;
};

struct PushHttpRequest {
  PushHttpMethod method;
  char url[256];
  char contentType[32];
  char body[4096];
  uint32_t timeoutMs;
};

struct PushHttpResult {
  bool success;
  int httpCode;
  char error[96];
};

struct ForwarderHttpDecision {
  bool shouldSend;
  SmsQueueItem* item;
};

class ForwarderHttpCore {
 public:
  static constexpr uint32_t kHttpTimeoutMs = 3000;
  static constexpr uint32_t kBaseRetryDelayMs = 5000;
  static constexpr uint32_t kMaxRetryDelayMs = 300000;

  void begin();
  ForwarderHttpDecision prepare(bool wifiConnected, SmsQueueCore& queue, uint32_t nowMs);
  void complete(SmsQueueCore& queue, SmsQueueItem* item, const PushHttpResult& result, uint32_t nowMs);
};

void forwarderHttpEscapeJson(const char* input, char* output, uint16_t outputSize);
uint32_t forwarderHttpRetryDelayMs(uint8_t attemptCount);
bool forwarderHttpBuildBarkRequest(const BarkChannelConfig& config,
                                   const SmsMessage& message,
                                   PushHttpRequest& request);
