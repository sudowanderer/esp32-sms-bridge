#pragma once

#include "logger_core.h"

#include <stdint.h>

void loggerBegin();
void logDebug(const char* message);
void logInfo(const char* message);
void logWarn(const char* message);
void logError(const char* message);
void loggerClear();
uint16_t loggerCount();
uint16_t loggerCapacity();
const LoggerEntry* loggerGet(uint16_t index);
