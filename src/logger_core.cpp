#include "logger_core.h"

#include <string.h>

void LoggerCore::begin() {
  start_ = 0;
  count_ = 0;
  nextSequence_ = 1;
  memset(entries_, 0, sizeof(entries_));
}

void LoggerCore::write(LoggerLevel level, const char* message, uint32_t timeMs) {
  uint16_t index = 0;
  if (count_ < kCapacity) {
    index = physicalIndex(count_);
    ++count_;
  } else {
    index = start_;
    start_ = static_cast<uint16_t>((start_ + 1) % kCapacity);
  }

  LoggerEntry& entry = entries_[index];
  entry.timeMs = timeMs;
  entry.sequence = nextSequence_++;
  entry.level = level;

  const char* safeMessage = message != nullptr ? message : "";
  strncpy(entry.message, safeMessage, sizeof(entry.message) - 1);
  entry.message[sizeof(entry.message) - 1] = '\0';
}

void LoggerCore::clear() {
  start_ = 0;
  count_ = 0;
  memset(entries_, 0, sizeof(entries_));
}

uint16_t LoggerCore::count() const {
  return count_;
}

uint16_t LoggerCore::capacity() const {
  return kCapacity;
}

const LoggerEntry* LoggerCore::get(uint16_t index) const {
  if (index >= count_) {
    return nullptr;
  }

  return &entries_[physicalIndex(index)];
}

uint16_t LoggerCore::physicalIndex(uint16_t logicalIndex) const {
  return static_cast<uint16_t>((start_ + logicalIndex) % kCapacity);
}

const char* loggerLevelName(LoggerLevel level) {
  switch (level) {
    case LoggerLevel::Debug:
      return "DEBUG";
    case LoggerLevel::Info:
      return "INFO";
    case LoggerLevel::Warn:
      return "WARN";
    case LoggerLevel::Error:
      return "ERROR";
  }

  return "UNKNOWN";
}
