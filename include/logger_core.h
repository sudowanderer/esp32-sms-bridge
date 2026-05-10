#pragma once

#include <stdint.h>

enum class LoggerLevel {
  Debug,
  Info,
  Warn,
  Error,
};

struct LoggerEntry {
  uint32_t timeMs;
  uint32_t sequence;
  LoggerLevel level;
  char message[192];
};

class LoggerCore {
public:
  static constexpr uint16_t kCapacity = 100;
  static constexpr uint16_t kMessageCapacity = sizeof(LoggerEntry::message);

  void begin();
  void write(LoggerLevel level, const char* message, uint32_t timeMs);
  void clear();

  uint16_t count() const;
  uint16_t capacity() const;
  const LoggerEntry* get(uint16_t index) const;

private:
  LoggerEntry entries_[kCapacity] = {};
  uint16_t start_ = 0;
  uint16_t count_ = 0;
  uint32_t nextSequence_ = 1;

  uint16_t physicalIndex(uint16_t logicalIndex) const;
};

const char* loggerLevelName(LoggerLevel level);
