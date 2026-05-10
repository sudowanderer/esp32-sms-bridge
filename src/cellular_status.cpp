#include "cellular_status.h"

#include "logger.h"
#include "modem_at.h"

#include <Arduino.h>
#include <stdio.h>

static constexpr uint32_t kInitialDelayMs = 5000;
static constexpr uint32_t kPollIntervalMs = 30000;
static constexpr uint32_t kCommandTimeoutMs = 3000;

static CellularStatusSnapshot snapshot = {};
static uint32_t nextPollMs = 0;
static bool pending = false;
static bool requestCsq = true;

static void handleCsqResult(ModemAtResult result, const char* response, void* userData) {
  (void)userData;
  pending = false;
  const uint32_t now = millis();
  if (result != ModemAtResult::Ok || !CellularStatusCore::parseCsqResponse(response, snapshot, now)) {
    nextPollMs = now + kPollIntervalMs;
    logWarn("cellular_status=csq_failed");
    return;
  }

  nextPollMs = now + 1000;

  char message[96];
  snprintf(message,
           sizeof(message),
           "cellular_signal csq=%u rssi_dbm=%d",
           snapshot.csqRssi,
           static_cast<int>(snapshot.rssiDbm));
  logInfo(message);
}

static void handleCeregResult(ModemAtResult result, const char* response, void* userData) {
  (void)userData;
  pending = false;
  const uint32_t now = millis();
  if (result != ModemAtResult::Ok || !CellularStatusCore::parseCeregResponse(response, snapshot, now)) {
    nextPollMs = now + kPollIntervalMs;
    logWarn("cellular_status=cereg_failed");
    return;
  }

  nextPollMs = now + kPollIntervalMs;

  char message[96];
  snprintf(message,
           sizeof(message),
           "cellular_registration status=%s",
           snapshot.registrationText);
  logInfo(message);
}

void cellularStatusBegin() {
  snapshot = {};
  pending = false;
  requestCsq = true;
  nextPollMs = millis() + kInitialDelayMs;
}

void cellularStatusPoll(uint32_t nowMs) {
  if (pending || static_cast<int32_t>(nowMs - nextPollMs) < 0) {
    return;
  }

  const char* command = requestCsq ? "AT+CSQ" : "AT+CEREG?";
  ModemAtCallback callback = requestCsq ? handleCsqResult : handleCeregResult;
  requestCsq = !requestCsq;
  nextPollMs = nowMs + kPollIntervalMs;

  if (!modemAtSubmit(command, kCommandTimeoutMs, callback, nullptr)) {
    nextPollMs = nowMs + 5000;
    logWarn("cellular_status=queue_full");
    return;
  }

  nextPollMs = nowMs + 5000;
  pending = true;
}

CellularStatusSnapshot cellularStatusGet() {
  return snapshot;
}
