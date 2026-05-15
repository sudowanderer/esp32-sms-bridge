#pragma once

#include "cellular_status_core.h"

#include <stdint.h>

void cellularStatusBegin();
void cellularStatusSetStartupComplete(bool complete);
void cellularStatusSetDataConnection(bool known, bool active, uint32_t nowMs);
void cellularStatusPoll(uint32_t nowMs);
CellularStatusSnapshot cellularStatusGet();
