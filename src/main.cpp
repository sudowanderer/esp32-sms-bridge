#include <Arduino.h>

#include "modem_at.h"
#include "sms_receiver.h"

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

static void printSms(const SmsMessage& message, void* userData) {
  (void)userData;

  Serial.println("sms_received_begin");
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
    Serial.print(message.concatPart);
    Serial.print("/");
    Serial.print(message.concatTotal);
    Serial.print(" ref=");
    Serial.println(message.concatRef);
    Serial.println("sms_concat_note=v0_detect_only_no_merge");
  }
  Serial.println("sms_received_end");
}

static void printSmsError(const char* reason, const char* rawLine, void* userData) {
  (void)userData;

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

  Serial.println();
  Serial.println("ESP32 SMS bridge sms_receiver v0 smoke test");
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
  smsReceiverSetCallback(printSms, nullptr);
  smsReceiverSetErrorCallback(printSmsError, nullptr);
  modemAtSetUrcCallback(handleModemUrc, nullptr);
  submitStartupCommands();
}

void loop() {
  modemAtPoll();
  smsReceiverPoll(millis());

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
    Serial.println(modemAtQueueDepth());
  }
}
