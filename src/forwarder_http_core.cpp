#include "forwarder_http_core.h"

#include <stdio.h>
#include <string.h>

static void appendChar(char value, char*& output, uint16_t& remaining) {
  if (remaining <= 1) {
    return;
  }

  *output++ = value;
  remaining--;
  *output = '\0';
}

static void appendString(const char* value, char*& output, uint16_t& remaining) {
  if (value == nullptr) {
    return;
  }

  while (*value != '\0' && remaining > 1) {
    appendChar(*value++, output, remaining);
  }
}

void ForwarderHttpCore::begin() {
}

ForwarderHttpDecision ForwarderHttpCore::prepare(bool wifiConnected, SmsQueueCore& queue, uint32_t nowMs) {
  ForwarderHttpDecision decision = {};
  if (!wifiConnected) {
    return decision;
  }

  SmsQueueItem* item = queue.acquireNext(nowMs);
  if (item == nullptr) {
    return decision;
  }

  queue.markSending(item, nowMs);
  decision.shouldSend = true;
  decision.item = item;
  return decision;
}

void ForwarderHttpCore::complete(SmsQueueCore& queue,
                                 SmsQueueItem* item,
                                 const PushHttpResult& result,
                                 uint32_t nowMs) {
  if (item == nullptr) {
    return;
  }

  if (result.success) {
    queue.markSent(item, nowMs);
    return;
  }

  const uint32_t retryDelayMs = forwarderHttpRetryDelayMs(item->attemptCount);
  const char* error = result.error[0] != '\0' ? result.error : "http_failed";
  queue.markFailed(item, error, retryDelayMs, nowMs);
}

void forwarderHttpEscapeJson(const char* input, char* output, uint16_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }

  output[0] = '\0';
  char* cursor = output;
  uint16_t remaining = outputSize;

  if (input == nullptr) {
    return;
  }

  while (*input != '\0' && remaining > 1) {
    switch (*input) {
      case '"':
        appendString("\\\"", cursor, remaining);
        break;
      case '\\':
        appendString("\\\\", cursor, remaining);
        break;
      case '\n':
        appendString("\\n", cursor, remaining);
        break;
      case '\r':
        appendString("\\r", cursor, remaining);
        break;
      case '\t':
        appendString("\\t", cursor, remaining);
        break;
      default:
        if (static_cast<unsigned char>(*input) < 0x20) {
          appendChar(' ', cursor, remaining);
        } else {
          appendChar(*input, cursor, remaining);
        }
        break;
    }
    input++;
  }
}

uint32_t forwarderHttpRetryDelayMs(uint8_t attemptCount) {
  uint32_t delayMs = ForwarderHttpCore::kBaseRetryDelayMs;
  for (uint8_t i = 0; i < attemptCount; ++i) {
    if (delayMs >= ForwarderHttpCore::kMaxRetryDelayMs / 2) {
      return ForwarderHttpCore::kMaxRetryDelayMs;
    }
    delayMs *= 2;
  }

  return delayMs > ForwarderHttpCore::kMaxRetryDelayMs ? ForwarderHttpCore::kMaxRetryDelayMs : delayMs;
}

bool forwarderHttpBuildBarkRequest(const BarkChannelConfig& config,
                                   const SmsMessage& message,
                                   PushHttpRequest& request) {
  if (config.serverUrl[0] == '\0' || config.deviceKey[0] == '\0') {
    return false;
  }

  memset(&request, 0, sizeof(request));
  request.method = PushHttpMethod::Post;
  request.timeoutMs = ForwarderHttpCore::kHttpTimeoutMs;
  strncpy(request.contentType, "application/json", sizeof(request.contentType) - 1);

  const int urlWritten = snprintf(request.url, sizeof(request.url), "%s/%s", config.serverUrl, config.deviceKey);
  if (urlWritten < 0 || static_cast<size_t>(urlWritten) >= sizeof(request.url)) {
    return false;
  }

  char escapedSender[96];
  char escapedTimestamp[64];
  char escapedText[576];
  forwarderHttpEscapeJson(message.sender, escapedSender, sizeof(escapedSender));
  forwarderHttpEscapeJson(message.timestamp, escapedTimestamp, sizeof(escapedTimestamp));
  forwarderHttpEscapeJson(message.text, escapedText, sizeof(escapedText));

  const int bodyWritten = snprintf(request.body,
                                   sizeof(request.body),
                                   "{\"title\":\"SMS from %s\",\"body\":\"%s\\n%s\",\"group\":\"ESP32 SMS Bridge\"}",
                                   escapedSender,
                                   escapedTimestamp,
                                   escapedText);
  return bodyWritten >= 0 && static_cast<size_t>(bodyWritten) < sizeof(request.body);
}
