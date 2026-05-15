#include "cellular_status.h"

#include "logger.h"
#include "modem_at.h"
#include "modem_commands.h"

#include <Arduino.h>
#include <stdio.h>

static constexpr uint32_t kInitialDelayMs = 5000;
static constexpr uint32_t kPollIntervalMs = 30000;
static constexpr uint32_t kStaticRetryIntervalMs = 600000;
static constexpr uint32_t kCommandTimeoutMs = 3000;
static constexpr uint32_t kBetweenCommandsMs = 1000;
static constexpr uint32_t kQueueRetryMs = 5000;

enum class QueryStep : uint8_t {
  ModuleInfo,
  Imsi,
  Iccid,
  OwnNumber,
  ExtendedSignal,
  Registration,
  Operator,
  PdpActivation,
  PdpContext,
  MipCall,
};

static constexpr QueryStep kStaticSteps[] = {
    QueryStep::ModuleInfo,
    QueryStep::Imsi,
    QueryStep::Iccid,
    QueryStep::OwnNumber,
};

static constexpr QueryStep kDynamicSteps[] = {
    QueryStep::ExtendedSignal,
    QueryStep::Registration,
    QueryStep::Operator,
    QueryStep::PdpContext,
    QueryStep::PdpActivation,
    QueryStep::MipCall,
};

static CellularStatusSnapshot snapshot = {};
static uint32_t nextPollMs = 0;
static uint32_t nextDynamicRoundMs = 0;
static uint32_t nextStaticRetryMs = 0;
static bool pending = false;
static bool activeStaticStep = false;
static bool staticRetryNeeded = false;
static uint8_t staticStepIndex = 0;
static uint8_t dynamicStepIndex = 0;
static QueryStep activeStep = QueryStep::ExtendedSignal;
static bool startupComplete = false;

static const char* commandForStep(QueryStep step) {
  switch (step) {
    case QueryStep::ModuleInfo:
      return ModemCommands::queryModuleInfo();
    case QueryStep::Imsi:
      return ModemCommands::queryImsi();
    case QueryStep::Iccid:
      return ModemCommands::queryIccid();
    case QueryStep::OwnNumber:
      return ModemCommands::queryOwnNumber();
    case QueryStep::ExtendedSignal:
      return ModemCommands::queryExtendedSignal();
    case QueryStep::Registration:
      return ModemCommands::queryRegistration();
    case QueryStep::Operator:
      return ModemCommands::queryOperator();
    case QueryStep::PdpActivation:
      return ModemCommands::queryPdpActivation();
    case QueryStep::PdpContext:
      return ModemCommands::queryPdpContext();
    case QueryStep::MipCall:
      return ModemCommands::queryMipCall();
  }
  return ModemCommands::queryExtendedSignal();
}

static const char* stepName(QueryStep step) {
  switch (step) {
    case QueryStep::ModuleInfo:
      return "module_info";
    case QueryStep::Imsi:
      return "imsi";
    case QueryStep::Iccid:
      return "iccid";
    case QueryStep::OwnNumber:
      return "own_number";
    case QueryStep::ExtendedSignal:
      return "cesq";
    case QueryStep::Registration:
      return "cereg";
    case QueryStep::Operator:
      return "cops";
    case QueryStep::PdpActivation:
      return "cgact";
    case QueryStep::PdpContext:
      return "cgdcont";
    case QueryStep::MipCall:
      return "mipcall";
  }
  return "unknown";
}

static bool parseStep(QueryStep step, const char* response, uint32_t now) {
  switch (step) {
    case QueryStep::ModuleInfo:
      return CellularStatusCore::parseAtiResponse(response, snapshot, now);
    case QueryStep::Imsi:
      return CellularStatusCore::parseImsiResponse(response, snapshot, now);
    case QueryStep::Iccid:
      return CellularStatusCore::parseIccidResponse(response, snapshot, now);
    case QueryStep::OwnNumber:
      return CellularStatusCore::parseCnumResponse(response, snapshot, now);
    case QueryStep::ExtendedSignal:
      return CellularStatusCore::parseCesqResponse(response, snapshot, now);
    case QueryStep::Registration:
      return CellularStatusCore::parseCeregResponse(response, snapshot, now);
    case QueryStep::Operator:
      return CellularStatusCore::parseCopsResponse(response, snapshot, now);
    case QueryStep::PdpActivation:
      return CellularStatusCore::parseCgactResponse(response, snapshot, now);
    case QueryStep::PdpContext:
      return CellularStatusCore::parseCgdccontResponse(response, snapshot, now);
    case QueryStep::MipCall:
      return CellularStatusCore::parseMipCallResponse(response, snapshot, now);
  }
  return false;
}

static void handleQueryResult(ModemAtResult result, const char* response, void* userData) {
  (void)userData;
  pending = false;
  const uint32_t now = millis();
  const bool ok = result == ModemAtResult::Ok && parseStep(activeStep, response, now);

  char message[112];
  if (ok) {
    snprintf(message, sizeof(message), "cellular_status step=%s ok", stepName(activeStep));
    logInfo(message);
    if (!activeStaticStep && activeStep == QueryStep::MipCall) {
      snprintf(message,
               sizeof(message),
               "cellular_status data_connection=%s apn=%s",
               snapshot.dataConnectionKnown ? (snapshot.dataConnectionActive ? "active" : "inactive") : "unknown",
               snapshot.apn[0] != '\0' ? snapshot.apn : "unknown");
      logInfo(message);
    }
  } else if (!activeStaticStep && activeStep == QueryStep::MipCall && snapshot.mipCallKnown && !snapshot.mipCallActive) {
    logInfo("cellular_status step=mipcall inactive_unconfirmed");
  } else {
    snprintf(message, sizeof(message), "cellular_status step=%s failed", stepName(activeStep));
    logWarn(message);
  }

  if (activeStaticStep) {
    if (!ok) {
      staticRetryNeeded = true;
    }
    ++staticStepIndex;
    if (staticStepIndex >= sizeof(kStaticSteps) / sizeof(kStaticSteps[0])) {
      nextStaticRetryMs = staticRetryNeeded ? now + kStaticRetryIntervalMs : 0;
    }
  } else {
    ++dynamicStepIndex;
    if (dynamicStepIndex >= sizeof(kDynamicSteps) / sizeof(kDynamicSteps[0])) {
      dynamicStepIndex = 0;
      nextDynamicRoundMs = now + kPollIntervalMs;
    }
  }

  nextPollMs = now + kBetweenCommandsMs;
}

void cellularStatusBegin() {
  snapshot = {};
  pending = false;
  activeStaticStep = false;
  staticRetryNeeded = false;
  staticStepIndex = 0;
  dynamicStepIndex = 0;
  const uint32_t now = millis();
  nextPollMs = now + kInitialDelayMs;
  nextDynamicRoundMs = now + kInitialDelayMs;
  nextStaticRetryMs = 0;
  startupComplete = false;
}

void cellularStatusSetStartupComplete(bool complete) {
  startupComplete = complete;
  if (complete) {
    const uint32_t now = millis();
    nextPollMs = now;
    nextDynamicRoundMs = now;
  }
}

void cellularStatusSetDataConnection(bool known, bool active, uint32_t nowMs) {
  snapshot.mipCallKnown = known;
  snapshot.mipCallActive = known && active;
  snapshot.dataConnectionKnown = known;
  snapshot.dataConnectionActive = known && active;
  snapshot.lastUpdatedMs = nowMs;
}

void cellularStatusPoll(uint32_t nowMs) {
  if (!startupComplete) {
    return;
  }

  if (pending || static_cast<int32_t>(nowMs - nextPollMs) < 0) {
    return;
  }

  if (staticStepIndex >= sizeof(kStaticSteps) / sizeof(kStaticSteps[0]) &&
      nextStaticRetryMs != 0 &&
      static_cast<int32_t>(nowMs - nextStaticRetryMs) >= 0) {
    staticStepIndex = 0;
    staticRetryNeeded = false;
  }

  if (staticStepIndex < sizeof(kStaticSteps) / sizeof(kStaticSteps[0])) {
    activeStaticStep = true;
    activeStep = kStaticSteps[staticStepIndex];
  } else if (static_cast<int32_t>(nowMs - nextDynamicRoundMs) >= 0) {
    activeStaticStep = false;
    activeStep = kDynamicSteps[dynamicStepIndex];
  } else {
    nextPollMs = nextDynamicRoundMs;
    return;
  }

  if (!modemAtSubmit(commandForStep(activeStep), kCommandTimeoutMs, handleQueryResult, nullptr)) {
    nextPollMs = nowMs + kQueueRetryMs;
    logWarn("cellular_status=queue_full");
    return;
  }

  nextPollMs = nowMs + kQueueRetryMs;
  pending = true;
}

CellularStatusSnapshot cellularStatusGet() {
  return snapshot;
}
