#include "web_server.h"

#include "cellular_status.h"
#include "config_store.h"
#include "forwarder_http.h"
#include "logger.h"
#include "modem_at.h"
#include "modem_events.h"
#include "sms_queue.h"
#include "sms_receive_watchdog.h"
#include "web_server_core.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <WebServer.h>
#include <string.h>

static WebServer server(80);
static bool started = false;
static char pageResponse[8192];

static void sendJsonBuffer(const char* json) {
  server.send(200, "application/json", json);
}

static void sendHtmlBuffer(const char* html) {
  server.send(200, "text/html; charset=utf-8", html);
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
    case WebConfigParseResult::InvalidHostname:
      return "invalid_hostname";
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
  const DeviceConfig& config = configStoreGet();
  CellularStatusSnapshot cellular = cellularStatusGet();
  snapshot.uptimeMs = millis();
  snapshot.freeHeap = ESP.getFreeHeap();
  snapshot.modemBusy = modemAtIsBusy();
  snapshot.modemQueueDepth = modemAtQueueDepth();
  modemEventsPoll(snapshot.uptimeMs);
  ModemEventsSnapshot modemEvents = modemEventsGet();
  snapshot.modemSimState = modemEvents.simState;
  snprintf(snapshot.modemLastEvent, sizeof(snapshot.modemLastEvent), "%s", modemEvents.lastEvent);
  snapshot.modemMatreadyCount = modemEvents.matreadyCount;
  snapshot.modemCpinReadyCount = modemEvents.cpinReadyCount;
  snapshot.modemSimRemovedCount = modemEvents.simRemovedCount;
  snapshot.modemLastMatreadyMs = modemEvents.lastMatreadyMs;
  snapshot.modemLastCpinReadyMs = modemEvents.lastCpinReadyMs;
  snapshot.modemLastSimRemovedMs = modemEvents.lastSimRemovedMs;
  snapshot.cellularSignalKnown = cellular.signalKnown;
  snapshot.cellularCsq = cellular.csqRssi;
  snapshot.cellularRssiDbm = cellular.rssiDbm;
  snapshot.cellularRegistrationKnown = cellular.registrationKnown;
  snapshot.cellularRegistrationStatus = cellular.registrationStatus;
  snprintf(snapshot.cellularRegistrationText, sizeof(snapshot.cellularRegistrationText), "%s", cellular.registrationText);
  snapshot.cellularLteSignalKnown = cellular.lteSignalKnown;
  snapshot.cellularRsrpDbm = cellular.rsrpDbm;
  snapshot.cellularRsrqDbTenths = cellular.rsrqDbTenths;
  snprintf(snapshot.cellularRsrpQuality,
           sizeof(snapshot.cellularRsrpQuality),
           "%s",
           CellularStatusCore::rsrpQualityName(cellular.rsrpDbm, cellular.lteSignalKnown));
  snprintf(snapshot.cellularCesqRaw, sizeof(snapshot.cellularCesqRaw), "%s", cellular.cesqRaw);
  snprintf(snapshot.cellularManufacturer, sizeof(snapshot.cellularManufacturer), "%s", cellular.manufacturer);
  snprintf(snapshot.cellularModel, sizeof(snapshot.cellularModel), "%s", cellular.model);
  snprintf(snapshot.cellularFirmware, sizeof(snapshot.cellularFirmware), "%s", cellular.firmware);
  snprintf(snapshot.cellularImsi, sizeof(snapshot.cellularImsi), "%s", cellular.imsi);
  snprintf(snapshot.cellularIccid, sizeof(snapshot.cellularIccid), "%s", cellular.iccid);
  snprintf(snapshot.cellularOwnNumber, sizeof(snapshot.cellularOwnNumber), "%s", cellular.ownNumber);
  snprintf(snapshot.cellularOperatorName, sizeof(snapshot.cellularOperatorName), "%s", cellular.operatorName);
  snapshot.cellularDataConnectionKnown = cellular.dataConnectionKnown;
  snapshot.cellularDataConnectionActive = cellular.dataConnectionActive;
  snprintf(snapshot.cellularApn, sizeof(snapshot.cellularApn), "%s", cellular.apn);
  snapshot.cellularLastUpdatedMs = cellular.lastUpdatedMs;
  snapshot.smsQueueDepth = smsQueueDepth();
  snapshot.smsQueuePending = smsQueuePendingCount();
  const SmsReceiveWatchdogSnapshot smsRx = smsReceiveWatchdogGetSnapshot();
  snapshot.smsCnmiKnown = smsRx.cnmiKnown;
  snprintf(snapshot.smsCnmiRaw, sizeof(snapshot.smsCnmiRaw), "%s", smsRx.cnmiRaw);
  snprintf(snapshot.smsCnmiDescription, sizeof(snapshot.smsCnmiDescription), "%s", smsRx.cnmiDescription);
  snapshot.smsCnmiLastUpdatedMs = smsRx.cnmiLastUpdatedMs;
  snapshot.wifiConfigured = wifiManagerIsConfigured();
  snapshot.wifiConnected = wifiManagerIsConnected();
  snprintf(snapshot.wifiStatus, sizeof(snapshot.wifiStatus), "%s", wifiManagerStatusName());
  if (wifiManagerIsConnected()) {
    snprintf(snapshot.wifiIp, sizeof(snapshot.wifiIp), "%s", wifiManagerLocalIp().toString().c_str());
  }
  snprintf(snapshot.wifiConfiguredHostname, sizeof(snapshot.wifiConfiguredHostname), "%s", config.hostname);
  snprintf(snapshot.wifiActiveHostname, sizeof(snapshot.wifiActiveHostname), "%s", wifiManagerHostname());
  snapshot.forwarderConfigured = forwarderHttpIsConfigured();
  snprintf(snapshot.forwarderStatus, sizeof(snapshot.forwarderStatus), "%s", forwarderHttpStatusName());
  snapshot.forwarderLastCode = forwarderHttpLastCode();
  snprintf(snapshot.forwarderLastError, sizeof(snapshot.forwarderLastError), "%s", forwarderHttpLastError());
  snapshot.loggerCount = loggerCount();

  char response[3072];
  if (!webBuildStatusJson(snapshot, response, sizeof(response))) {
    sendJsonError(500, "status_json_too_large");
    return;
  }

  sendJsonBuffer(response);
}

static void handlePage(WebPageKind page) {
  if (!webBuildPageHtml(page, pageResponse, sizeof(pageResponse))) {
    sendJsonError(500, "page_too_large");
    return;
  }

  sendHtmlBuffer(pageResponse);
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
  const DeviceConfig previousConfig = configStoreGet();
  DeviceConfig config = previousConfig;
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
  const bool reconnectRequired = strcmp(previousConfig.hostname, configStoreGet().hostname) != 0;
  if (reconnectRequired) {
    logInfo("wifi_hostname_status=changed reconnect_required");
  }

  char response[512];
  char configJson[512];
  if (!webBuildConfigJson(configStoreGet(), configJson, sizeof(configJson))) {
    sendJsonError(500, "config_json_too_large");
    return;
  }
  const int written = snprintf(response,
                               sizeof(response),
                               "{\"config\":%s,\"restart_or_reconnect_required\":%s}",
                               configJson,
                               reconnectRequired ? "true" : "false");
  if (written < 0 || static_cast<size_t>(written) >= sizeof(response)) {
    sendJsonError(500, "config_json_too_large");
    return;
  }

  sendJsonBuffer(response);
}

static void handleWifiReconnect() {
  const bool accepted = wifiManagerRequestReconnect();
  server.send(accepted ? 202 : 409, "application/json", accepted ? "{\"status\":\"queued\"}" : "{\"error\":\"wifi_unconfigured\"}");
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
  server.on("/api/wifi/reconnect", HTTP_POST, handleWifiReconnect);
  server.on("/", HTTP_GET, []() {
    handlePage(WebPageKind::Status);
  });
  server.on("/config", HTTP_GET, []() {
    handlePage(WebPageKind::Config);
  });
  server.on("/queue", HTTP_GET, []() {
    handlePage(WebPageKind::Queue);
  });
  server.on("/logs", HTTP_GET, []() {
    handlePage(WebPageKind::Logs);
  });
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
