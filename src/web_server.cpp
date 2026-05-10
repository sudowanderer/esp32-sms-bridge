#include "web_server.h"

#include "config_store.h"
#include "forwarder_http.h"
#include "logger.h"
#include "modem_at.h"
#include "sms_queue.h"
#include "web_server_core.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <WebServer.h>

static WebServer server(80);
static bool started = false;

static void sendJsonBuffer(const char* json) {
  server.send(200, "application/json", json);
}

static void sendJsonError(int code, const char* error) {
  char response[96];
  snprintf(response, sizeof(response), "{\"error\":\"%s\"}", error != nullptr ? error : "unknown");
  server.send(code, "application/json", response);
}

static const char* configParseErrorName(WebConfigParseResult result) {
  switch (result) {
    case WebConfigParseResult::Ok:
      return "";
    case WebConfigParseResult::InvalidJson:
      return "invalid_json";
    case WebConfigParseResult::ValueTooLong:
      return "value_too_long";
  }

  return "invalid_config";
}

static uint16_t requestedLimit(uint16_t defaultLimit, uint16_t maxLimit) {
  if (!server.hasArg("limit")) {
    return defaultLimit;
  }

  const int value = server.arg("limit").toInt();
  if (value <= 0) {
    return defaultLimit;
  }
  if (value > maxLimit) {
    return maxLimit;
  }
  return static_cast<uint16_t>(value);
}

static void handleStatus() {
  WebStatusSnapshot snapshot = {};
  snapshot.uptimeMs = millis();
  snapshot.freeHeap = ESP.getFreeHeap();
  snapshot.modemBusy = modemAtIsBusy();
  snapshot.modemQueueDepth = modemAtQueueDepth();
  snapshot.smsQueueDepth = smsQueueDepth();
  snapshot.smsQueuePending = smsQueuePendingCount();
  snapshot.wifiConfigured = wifiManagerIsConfigured();
  snapshot.wifiConnected = wifiManagerIsConnected();
  snprintf(snapshot.wifiStatus, sizeof(snapshot.wifiStatus), "%s", wifiManagerStatusName());
  if (wifiManagerIsConnected()) {
    snprintf(snapshot.wifiIp, sizeof(snapshot.wifiIp), "%s", wifiManagerLocalIp().toString().c_str());
  }
  snapshot.forwarderConfigured = forwarderHttpIsConfigured();
  snprintf(snapshot.forwarderStatus, sizeof(snapshot.forwarderStatus), "%s", forwarderHttpStatusName());
  snapshot.forwarderLastCode = forwarderHttpLastCode();
  snprintf(snapshot.forwarderLastError, sizeof(snapshot.forwarderLastError), "%s", forwarderHttpLastError());
  snapshot.loggerCount = loggerCount();

  char response[1024];
  if (!webBuildStatusJson(snapshot, response, sizeof(response))) {
    sendJsonError(500, "status_json_too_large");
    return;
  }

  sendJsonBuffer(response);
}

static void handleLogs() {
  const uint16_t count = loggerCount();
  const uint16_t limit = requestedLimit(50, loggerCapacity());
  const uint16_t start = count > limit ? static_cast<uint16_t>(count - limit) : 0;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"logs\":[");

  char entryJson[320];
  bool first = true;
  for (uint16_t i = start; i < count; ++i) {
    const LoggerEntry* entry = loggerGet(i);
    if (entry == nullptr) {
      continue;
    }
    if (!webBuildLogEntryJson(*entry, entryJson, sizeof(entryJson))) {
      continue;
    }
    if (!first) {
      server.sendContent(",");
    }
    server.sendContent(entryJson);
    first = false;
  }

  char tail[64];
  snprintf(tail, sizeof(tail), "],\"count\":%u,\"limit\":%u}", count, limit);
  server.sendContent(tail);
  server.sendContent("");
}

static void handleQueue() {
  const uint8_t count = smsQueueDepth();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"queue\":[");

  char itemJson[512];
  bool first = true;
  for (uint8_t i = 0; i < count; ++i) {
    const SmsQueueItem* item = smsQueueGet(i);
    if (item == nullptr) {
      continue;
    }
    if (!webBuildQueueItemJson(*item, i, itemJson, sizeof(itemJson))) {
      continue;
    }
    if (!first) {
      server.sendContent(",");
    }
    server.sendContent(itemJson);
    first = false;
  }

  char tail[64];
  snprintf(tail, sizeof(tail), "],\"depth\":%u,\"pending\":%u}", count, smsQueuePendingCount());
  server.sendContent(tail);
  server.sendContent("");
}

static void handleConfigGet() {
  char response[512];
  if (!webBuildConfigJson(configStoreGet(), response, sizeof(response))) {
    sendJsonError(500, "config_json_too_large");
    return;
  }

  sendJsonBuffer(response);
}

static void handleConfigSave() {
  const String body = server.arg("plain");
  DeviceConfig config = configStoreGet();
  const WebConfigParseResult parseResult = webParseConfigSaveJson(body.c_str(), config);
  if (parseResult != WebConfigParseResult::Ok) {
    logWarn("web_config_status=invalid_request");
    sendJsonError(400, configParseErrorName(parseResult));
    return;
  }

  if (!configStoreSave(config)) {
    logError("web_config_status=save_failed");
    sendJsonError(500, "config_save_failed");
    return;
  }

  logInfo("web_config_status=saved");
  char response[512];
  if (!webBuildConfigJson(configStoreGet(), response, sizeof(response))) {
    sendJsonError(500, "config_json_too_large");
    return;
  }

  sendJsonBuffer(response);
}

void webServerBegin() {
  if (started) {
    return;
  }

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/queue", HTTP_GET, handleQueue);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config/save", HTTP_POST, handleConfigSave);
  server.onNotFound([]() {
    sendJsonError(404, "not_found");
  });
  server.begin();
  started = true;
  logInfo("web_server_status=started port=80");
}

void webServerPoll() {
  if (!started) {
    return;
  }

  server.handleClient();
}
