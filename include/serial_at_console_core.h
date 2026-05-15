#pragma once

#include <stddef.h>

enum class SerialAtConsoleAction {
  None,
  Help,
  Submit,
  Busy,
  RejectedUnsafe,
  Invalid,
  TooLong,
};

class SerialAtConsoleCore {
 public:
  static constexpr size_t kLineCapacity = 128;
  static constexpr size_t kCommandCapacity = 96;

  void begin();
  SerialAtConsoleAction onChar(char c, bool commandPending, char* commandOut, size_t commandOutSize);

 private:
  SerialAtConsoleAction processLine(bool commandPending, char* commandOut, size_t commandOutSize);
  static char* trim(char* value);
  static bool startsWithIgnoreCase(const char* value, const char* prefix);
  static bool equalsIgnoreCase(const char* left, const char* right);
  static bool isUnsafeCommand(const char* command);

  char line_[kLineCapacity];
  size_t lineLen_ = 0;
  bool overflowed_ = false;
};
