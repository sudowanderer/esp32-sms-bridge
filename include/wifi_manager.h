#pragma once

#include "wifi_manager_core.h"

#include <IPAddress.h>
#include <stdint.h>

void wifiManagerBegin();
void wifiManagerPoll(uint32_t nowMs);

bool wifiManagerIsConfigured();
bool wifiManagerIsConnected();
WifiManagerStatus wifiManagerStatus();
const char* wifiManagerStatusName();
IPAddress wifiManagerLocalIp();
