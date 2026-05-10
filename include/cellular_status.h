#pragma once

#include "cellular_status_core.h"

#include <stdint.h>

void cellularStatusBegin();
void cellularStatusPoll(uint32_t nowMs);
CellularStatusSnapshot cellularStatusGet();
