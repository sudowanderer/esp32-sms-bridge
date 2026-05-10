#include "forwarder_http.h"

#include "forwarder_http_core.h"
#include "sms_queue.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#if __has_include("local_push_config.h")
#include "local_push_config.h"
#else
#define PUSH_BARK_SERVER_URL ""
#define PUSH_BARK_DEVICE_KEY ""
#endif

enum class ForwarderHttpStatus {
  Unconfigured,
  Idle,
  Sending,
  LastSuccess,
  LastFailed,
};

static ForwarderHttpStatus status = ForwarderHttpStatus::Unconfigured;
static int lastCode = 0;
static char lastError[96] = "";
static bool printedUnconfigured = false;
static PushHttpRequest currentRequest;

static BarkChannelConfig barkConfig() {
  BarkChannelConfig config = {};
  strncpy(config.serverUrl, PUSH_BARK_SERVER_URL, sizeof(config.serverUrl) - 1);
  strncpy(config.deviceKey, PUSH_BARK_DEVICE_KEY, sizeof(config.deviceKey) - 1);
  return config;
}

static bool isHttpsUrl(const char* url) {
  return strncmp(url, "https://", strlen("https://")) == 0;
}

static void setLastError(const char* error) {
  if (error == nullptr) {
    lastError[0] = '\0';
    return;
  }

  strncpy(lastError, error, sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
}

static PushHttpResult executeHttpRequest(const PushHttpRequest& request) {
  PushHttpResult result = {};
  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;

  secureClient.setInsecure();
  const bool begun = isHttpsUrl(request.url) ? http.begin(secureClient, request.url) : http.begin(plainClient, request.url);
  if (!begun) {
    setLastError("http_begin_failed");
    strncpy(result.error, "http_begin_failed", sizeof(result.error) - 1);
    return result;
  }

  http.setTimeout(request.timeoutMs);
  http.addHeader("Content-Type", request.contentType);

  const int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(request.body)), strlen(request.body));
  result.httpCode = code;
  lastCode = code;

  if (code >= 200 && code < 300) {
    result.success = true;
  } else if (code > 0) {
    snprintf(result.error, sizeof(result.error), "http_%d", code);
  } else {
    strncpy(result.error, http.errorToString(code).c_str(), sizeof(result.error) - 1);
    result.error[sizeof(result.error) - 1] = '\0';
  }

  http.end();
  return result;
}

void forwarderHttpBegin() {
  lastCode = 0;
  setLastError("");

  if (!forwarderHttpIsConfigured()) {
    status = ForwarderHttpStatus::Unconfigured;
    if (!printedUnconfigured) {
      Serial.println("forwarder_http_status=unconfigured");
      printedUnconfigured = true;
    }
    return;
  }

  status = ForwarderHttpStatus::Idle;
  Serial.println("forwarder_http_status=idle");
}

void forwarderHttpPoll(uint32_t nowMs) {
  if (!forwarderHttpIsConfigured()) {
    status = ForwarderHttpStatus::Unconfigured;
    return;
  }

  if (!wifiManagerIsConnected()) {
    if (status != ForwarderHttpStatus::Idle) {
      status = ForwarderHttpStatus::Idle;
    }
    return;
  }

  SmsQueueItem* item = smsQueueAcquireNext(nowMs);
  if (item == nullptr) {
    if (status == ForwarderHttpStatus::Sending) {
      status = ForwarderHttpStatus::Idle;
    }
    return;
  }

  if (!forwarderHttpBuildBarkRequest(barkConfig(), item->message, currentRequest)) {
    smsQueueMarkFailed(item, "push_config_invalid", ForwarderHttpCore::kMaxRetryDelayMs, nowMs);
    status = ForwarderHttpStatus::LastFailed;
    setLastError("push_config_invalid");
    Serial.println("forwarder_http_result=failed error=push_config_invalid");
    return;
  }

  smsQueueMarkSending(item, nowMs);
  status = ForwarderHttpStatus::Sending;
  Serial.print("forwarder_http_send sender=");
  Serial.println(item->message.sender);

  PushHttpResult result = executeHttpRequest(currentRequest);
  if (result.success) {
    smsQueueMarkSent(item, millis());
    status = ForwarderHttpStatus::LastSuccess;
    setLastError("");
    Serial.print("forwarder_http_result=sent code=");
    Serial.println(result.httpCode);
    return;
  }

  const uint32_t retryDelayMs = forwarderHttpRetryDelayMs(item->attemptCount);
  const char* error = result.error[0] != '\0' ? result.error : "http_failed";
  smsQueueMarkFailed(item, error, retryDelayMs, millis());
  status = ForwarderHttpStatus::LastFailed;
  setLastError(error);
  Serial.print("forwarder_http_result=failed code=");
  Serial.print(result.httpCode);
  Serial.print(" error=");
  Serial.println(error);
}

bool forwarderHttpIsConfigured() {
  return PUSH_BARK_SERVER_URL[0] != '\0' && PUSH_BARK_DEVICE_KEY[0] != '\0';
}

const char* forwarderHttpStatusName() {
  switch (status) {
    case ForwarderHttpStatus::Unconfigured:
      return "unconfigured";
    case ForwarderHttpStatus::Idle:
      return "idle";
    case ForwarderHttpStatus::Sending:
      return "sending";
    case ForwarderHttpStatus::LastSuccess:
      return "last_success";
    case ForwarderHttpStatus::LastFailed:
      return "last_failed";
  }

  return "unknown";
}

int forwarderHttpLastCode() {
  return lastCode;
}

const char* forwarderHttpLastError() {
  return lastError;
}
