#include "serial_at_console_core.h"

#include <ctype.h>
#include <string.h>

void SerialAtConsoleCore::begin() {
  lineLen_ = 0;
  line_[0] = '\0';
  overflowed_ = false;
}

SerialAtConsoleAction SerialAtConsoleCore::onChar(char c,
                                                  bool commandPending,
                                                  char* commandOut,
                                                  size_t commandOutSize) {
  if (c == '\r') {
    return SerialAtConsoleAction::None;
  }

  if (c == '\b' || c == 0x7f) {
    if (lineLen_ > 0) {
      lineLen_--;
      line_[lineLen_] = '\0';
    }
    return SerialAtConsoleAction::None;
  }

  if (c == '\n') {
    if (overflowed_) {
      begin();
      return SerialAtConsoleAction::TooLong;
    }

    line_[lineLen_] = '\0';
    const SerialAtConsoleAction action = processLine(commandPending, commandOut, commandOutSize);
    lineLen_ = 0;
    line_[0] = '\0';
    return action;
  }

  if (lineLen_ + 1 >= kLineCapacity) {
    overflowed_ = true;
    return SerialAtConsoleAction::None;
  }

  line_[lineLen_++] = c;
  line_[lineLen_] = '\0';
  return SerialAtConsoleAction::None;
}

SerialAtConsoleAction SerialAtConsoleCore::processLine(bool commandPending,
                                                       char* commandOut,
                                                       size_t commandOutSize) {
  char* line = trim(line_);
  if (line[0] == '\0') {
    return SerialAtConsoleAction::None;
  }

  if (equalsIgnoreCase(line, "help")) {
    return SerialAtConsoleAction::Help;
  }

  if (!startsWithIgnoreCase(line, "at ")) {
    return SerialAtConsoleAction::Invalid;
  }

  char* command = trim(line + 3);
  if (!startsWithIgnoreCase(command, "AT")) {
    return SerialAtConsoleAction::Invalid;
  }

  if (isUnsafeCommand(command)) {
    return SerialAtConsoleAction::RejectedUnsafe;
  }

  if (commandPending) {
    return SerialAtConsoleAction::Busy;
  }

  if (commandOut == nullptr || commandOutSize == 0 || strlen(command) + 1 > commandOutSize) {
    return SerialAtConsoleAction::TooLong;
  }

  strncpy(commandOut, command, commandOutSize - 1);
  commandOut[commandOutSize - 1] = '\0';
  return SerialAtConsoleAction::Submit;
}

char* SerialAtConsoleCore::trim(char* value) {
  if (value == nullptr) {
    return value;
  }

  while (*value != '\0' && isspace(static_cast<unsigned char>(*value))) {
    value++;
  }

  char* end = value + strlen(value);
  while (end > value && isspace(static_cast<unsigned char>(*(end - 1)))) {
    end--;
  }
  *end = '\0';
  return value;
}

bool SerialAtConsoleCore::startsWithIgnoreCase(const char* value, const char* prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }

  while (*prefix != '\0') {
    if (*value == '\0') {
      return false;
    }

    if (toupper(static_cast<unsigned char>(*value)) != toupper(static_cast<unsigned char>(*prefix))) {
      return false;
    }

    value++;
    prefix++;
  }

  return true;
}

bool SerialAtConsoleCore::equalsIgnoreCase(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }

  while (*left != '\0' && *right != '\0') {
    if (toupper(static_cast<unsigned char>(*left)) != toupper(static_cast<unsigned char>(*right))) {
      return false;
    }
    left++;
    right++;
  }

  return *left == '\0' && *right == '\0';
}

bool SerialAtConsoleCore::isUnsafeCommand(const char* command) {
  return startsWithIgnoreCase(command, "AT+CMGD") || startsWithIgnoreCase(command, "AT+CFUN=") ||
         startsWithIgnoreCase(command, "AT+CGACT=") || startsWithIgnoreCase(command, "AT+MIPCALL=1") ||
         startsWithIgnoreCase(command, "AT+MIPCALL=0");
}
