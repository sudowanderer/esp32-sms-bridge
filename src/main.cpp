#include <Arduino.h>

#include "cellular_status.h"
#include "config_store.h"
#include "forwarder_http.h"
#include "logger.h"
#include "modem_at.h"
#include "sms_queue.h"
#include "sms_receiver.h"
#include "web_server.h"
#include "wifi_manager.h"

static constexpr uint32_t kLogIntervalMs = 1000;
static constexpr uint32_t kCommandTimeoutMs = 3000;

static uint32_t lastLogMs = 0;

static void printAtResult(ModemAtResult result, const char* response, void* userData) {
  const char* command = static_cast<const char*>(userData);

  Serial.print("at_command=");
  Serial.print(command);
  Serial.print(" result=");

  switch (result) {
    case ModemAtResult::Ok:
      Serial.println("OK");
      break;
    case ModemAtResult::Error:
      Serial.println("ERROR");
      break;
    case ModemAtResult::Timeout:
      Serial.println("TIMEOUT");
      break;
    case ModemAtResult::QueueFull:
      Serial.println("QUEUE_FULL");
      break;
  }

  if (response != nullptr && response[0] != '\0') {
    Serial.println("at_response_begin");
    Serial.print(response);
    Serial.println("at_response_end");
  }
}

static void printSmsDebug(const SmsMessage& message, bool enqueued) {
  Serial.println("sms_received_begin");
  Serial.print("sms_queue=");
  Serial.println(enqueued ? "enqueued" : "full");
  Serial.print("sms_queue_depth=");
  Serial.println(smsQueueDepth());
  Serial.print("sms_sender=");
  Serial.println(message.sender);
  Serial.print("sms_timestamp=");
  Serial.println(message.timestamp);
  Serial.print("sms_text=");
  Serial.println(message.text);
  Serial.print("sms_pdu=");
  Serial.println(message.pdu);
  if (message.isConcat) {
    Serial.print("sms_concat=");
    if (message.concatComplete) {
      Serial.print("merged total=");
      Serial.print(message.concatTotal);
    } else if (message.concatPartial) {
      Serial.print("partial total=");
      Serial.print(message.concatTotal);
    } else {
      Serial.print(message.concatPart);
      Serial.print("/");
      Serial.print(message.concatTotal);
    }
    Serial.print(" ref=");
    Serial.println(message.concatRef);
  }
  Serial.println("sms_received_end");
}

static void handleSmsReceived(const SmsMessage& message, void* userData) {
  (void)userData;

  const uint32_t now = millis();
  const bool enqueued = smsQueueEnqueue(message, now);
  char logMessage[160];
  snprintf(logMessage,
           sizeof(logMessage),
           "sms_received sender=%s queued=%s concat=%s",
           message.sender,
           enqueued ? "yes" : "no",
           message.isConcat ? (message.concatComplete ? "merged" : (message.concatPartial ? "partial" : "part")) : "no");
  if (enqueued) {
    logInfo(logMessage);
  } else {
    logError(logMessage);
  }
  printSmsDebug(message, enqueued);
}

static void printSmsError(const char* reason, const char* rawLine, void* userData) {
  (void)userData;

  char logMessage[160];
  snprintf(logMessage, sizeof(logMessage), "sms_error=%s", reason != nullptr ? reason : "");
  logError(logMessage);

  Serial.print("sms_error=");
  Serial.print(reason);
  Serial.print(" raw=");
  Serial.println(rawLine != nullptr ? rawLine : "");
}

static void handleModemUrc(const char* line, void* userData) {
  (void)userData;

  if (smsReceiverOnUrc(line)) {
    return;
  }

  Serial.print("modem_urc=");
  Serial.println(line);
}

static void submitStartupCommand(const char* command) {
  if (!modemAtSubmit(command, kCommandTimeoutMs, printAtResult, const_cast<char*>(command))) {
    char message[96];
    snprintf(message, sizeof(message), "at_command=%s result=QUEUE_FULL", command);
    logWarn(message);
    Serial.print("at_command=");
    Serial.print(command);
    Serial.println(" result=QUEUE_FULL");
  }
}

static void submitStartupCommands() {
  submitStartupCommand("AT");
  submitStartupCommand("AT+CMGF=0");
  submitStartupCommand("AT+CNMI=2,2,0,0,0");
  submitStartupCommand("AT+CEREG?");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  loggerBegin();

  Serial.println();
  Serial.println("ESP32 SMS bridge sms_receiver v0 smoke test");
  logInfo("system_boot app=sms_receiver_v0");
  Serial.print("chip_model=");
  Serial.println(ESP.getChipModel());
  Serial.print("chip_revision=");
  Serial.println(ESP.getChipRevision());
  Serial.print("cpu_mhz=");
  Serial.println(ESP.getCpuFreqMHz());
  Serial.print("flash_size=");
  Serial.println(ESP.getFlashChipSize());

  modemAtBegin();
  smsReceiverBegin();
  smsQueueBegin();
  configStoreBegin();
  wifiManagerBegin();
  cellularStatusBegin();
  forwarderHttpBegin();
  webServerBegin();
  smsReceiverSetCallback(handleSmsReceived, nullptr);
  smsReceiverSetErrorCallback(printSmsError, nullptr);
  modemAtSetUrcCallback(handleModemUrc, nullptr);
  submitStartupCommands();
}

void loop() {
  modemAtPoll();
  smsReceiverPoll(millis());
  cellularStatusPoll(millis());
  wifiManagerPoll(millis());
  webServerPoll();
  forwarderHttpPoll(millis());

  const uint32_t now = millis();
  if (now - lastLogMs >= kLogIntervalMs) {
    lastLogMs = now;

    Serial.print("uptime_ms=");
    Serial.print(now);
    Serial.print(" free_heap=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" modem_busy=");
    Serial.print(modemAtIsBusy() ? "yes" : "no");
    Serial.print(" modem_queue_depth=");
    Serial.print(modemAtQueueDepth());
    Serial.print(" sms_queue_pending=");
    Serial.print(smsQueuePendingCount());
    Serial.print(" wifi_status=");
    Serial.print(wifiManagerStatusName());
    if (wifiManagerIsConnected()) {
      Serial.print(" wifi_ip=");
      Serial.print(wifiManagerLocalIp());
    }
    Serial.print(" forwarder_http_status=");
    Serial.print(forwarderHttpStatusName());
    if (forwarderHttpLastCode() != 0) {
      Serial.print(" forwarder_http_code=");
      Serial.print(forwarderHttpLastCode());
    }
    if (forwarderHttpLastError()[0] != '\0') {
      Serial.print(" forwarder_http_error=");
      Serial.print(forwarderHttpLastError());
    }
    Serial.println();
  }
}
