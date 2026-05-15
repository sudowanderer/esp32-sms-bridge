#include "serial_at_console.h"

#include "modem_at.h"
#include "serial_at_console_core.h"

#include <Arduino.h>

#ifndef ENABLE_SERIAL_AT_CONSOLE
#define ENABLE_SERIAL_AT_CONSOLE 0
#endif

static constexpr uint32_t kManualAtTimeoutMs = 10000;

static SerialAtConsoleCore serialAtConsoleCore;
static bool manualAtPending = false;
static char manualAtCommand[SerialAtConsoleCore::kCommandCapacity];

static void printPrompt() {
  Serial.print("at> ");
}

static void printManualAtResult(ModemAtResult result, const char* response, void* userData) {
  const char* command = static_cast<const char*>(userData);

  Serial.print("manual_at command=");
  Serial.print(command != nullptr ? command : "");
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
    Serial.println("manual_at_response_begin");
    Serial.print(response);
    Serial.println("manual_at_response_end");
  }

  manualAtPending = false;
  printPrompt();
}

static void printManualAtHelp() {
  Serial.println("serial_at_console_help_begin");
  Serial.println("help");
  Serial.println("at AT");
  Serial.println("at AT+MIPCALL?");
  Serial.println("at AT+CGDCONT?");
  Serial.println("serial_at_console_help_end");
}

static void handleConsoleAction(SerialAtConsoleAction action, const char* command) {
  switch (action) {
    case SerialAtConsoleAction::None:
      return;
    case SerialAtConsoleAction::Help:
      printManualAtHelp();
      printPrompt();
      return;
    case SerialAtConsoleAction::Submit:
      manualAtPending = true;
      if (!modemAtSubmit(command, kManualAtTimeoutMs, printManualAtResult, manualAtCommand)) {
        manualAtPending = false;
        Serial.print("manual_at command=");
        Serial.print(command);
        Serial.println(" result=QUEUE_FULL");
        printPrompt();
      }
      return;
    case SerialAtConsoleAction::Busy:
      Serial.println("manual_at result=BUSY");
      printPrompt();
      return;
    case SerialAtConsoleAction::RejectedUnsafe:
      Serial.println("manual_at rejected=unsafe_command");
      printPrompt();
      return;
    case SerialAtConsoleAction::Invalid:
      Serial.println("manual_at rejected=invalid_command");
      printPrompt();
      return;
    case SerialAtConsoleAction::TooLong:
      Serial.println("manual_at rejected=too_long");
      printPrompt();
      return;
  }
}

static void echoInputChar(char c) {
  if (c == '\r') {
    return;
  }

  if (c == '\n') {
    Serial.println();
    return;
  }

  if (c == '\b' || c == 0x7f) {
    Serial.print("\b \b");
    return;
  }

  if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) != 0x7f) {
    Serial.print(c);
  }
}

void serialAtConsoleBegin() {
#if ENABLE_SERIAL_AT_CONSOLE
  serialAtConsoleCore.begin();
  manualAtPending = false;
  manualAtCommand[0] = '\0';
  Serial.println("serial_at_console=enabled prefix=at");
  printPrompt();
#endif
}

void serialAtConsolePoll(uint32_t nowMs) {
  (void)nowMs;
#if ENABLE_SERIAL_AT_CONSOLE
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    echoInputChar(c);
    const SerialAtConsoleAction action =
        serialAtConsoleCore.onChar(c, manualAtPending, manualAtCommand, sizeof(manualAtCommand));
    handleConsoleAction(action, manualAtCommand);
  }
#endif
}
