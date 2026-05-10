#pragma once

#include "config_store_core.h"
#include "logger_core.h"
#include "sms_queue_core.h"

#include <stddef.h>
#include <stdint.h>

struct WebStatusSnapshot {
  uint32_t uptimeMs;
  uint32_t freeHeap;
  bool modemBusy;
  uint8_t modemQueueDepth;
  uint8_t smsQueueDepth;
  uint8_t smsQueuePending;
  bool wifiConfigured;
  bool wifiConnected;
  char wifiStatus[24];
  char wifiIp[40];
  bool forwarderConfigured;
  char forwarderStatus[32];
  int forwarderLastCode;
  char forwarderLastError[96];
  uint16_t loggerCount;
};

enum class WebConfigParseResult {
  Ok,
  InvalidJson,
  ValueTooLong,
};

bool webJsonEscape(const char* input, char* output, size_t outputSize);
bool webBuildStatusJson(const WebStatusSnapshot& status, char* output, size_t outputSize);
bool webBuildLogEntryJson(const LoggerEntry& entry, char* output, size_t outputSize);
bool webBuildQueueItemJson(const SmsQueueItem& item, uint8_t index, char* output, size_t outputSize);
bool webBuildConfigJson(const DeviceConfig& config, char* output, size_t outputSize);
WebConfigParseResult webParseConfigSaveJson(const char* json, DeviceConfig& config);
const char* webSmsQueueStatusName(SmsQueueStatus status);
