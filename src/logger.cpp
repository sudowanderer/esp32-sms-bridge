#include "logger.h"

#include <Arduino.h>

static LoggerCore loggerCore;

static void writeLog(LoggerLevel level, const char* message) {
  const char* safeMessage = message != nullptr ? message : "";
  loggerCore.write(level, safeMessage, millis());

  Serial.print("log_level=");
  Serial.print(loggerLevelName(level));
  Serial.print(" message=");
  Serial.println(safeMessage);
}

void loggerBegin() {
  loggerCore.begin();
}

void logDebug(const char* message) {
  writeLog(LoggerLevel::Debug, message);
}

void logInfo(const char* message) {
  writeLog(LoggerLevel::Info, message);
}

void logWarn(const char* message) {
  writeLog(LoggerLevel::Warn, message);
}

void logError(const char* message) {
  writeLog(LoggerLevel::Error, message);
}

void loggerClear() {
  loggerCore.clear();
}

uint16_t loggerCount() {
  return loggerCore.count();
}

uint16_t loggerCapacity() {
  return loggerCore.capacity();
}

const LoggerEntry* loggerGet(uint16_t index) {
  return loggerCore.get(index);
}
