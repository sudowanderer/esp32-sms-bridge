#pragma once

#include "config_store_core.h"

void configStoreBegin();
bool configStoreLoad(DeviceConfig& config);
bool configStoreSave(const DeviceConfig& config);
bool configStoreReset();
const DeviceConfig& configStoreGet();
bool configStoreHasSavedConfig();
