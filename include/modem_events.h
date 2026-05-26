#pragma once

#include "modem_events_core.h"

#include <stdint.h>

void modemEventsBegin();
bool modemEventsOnUrc(const char* line, uint32_t nowMs);
void modemEventsPoll(uint32_t nowMs);
ModemEventsSnapshot modemEventsGet();
