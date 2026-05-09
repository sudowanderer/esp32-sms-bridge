#pragma once

#include <stdint.h>

void forwarderHttpBegin();
void forwarderHttpPoll(uint32_t nowMs);

bool forwarderHttpIsConfigured();
const char* forwarderHttpStatusName();
int forwarderHttpLastCode();
const char* forwarderHttpLastError();
