#include "config_store_core.h"

#include <string.h>

void ConfigStoreCore::defaults(DeviceConfig& config) {
  memset(&config, 0, sizeof(config));
  config.version = kCurrentVersion;
}

void ConfigStoreCore::applyFallback(DeviceConfig& config, const DeviceConfigFallback& fallback) {
  defaults(config);
  copyString(config.wifiSsid, sizeof(config.wifiSsid), fallback.wifiSsid);
  copyString(config.wifiPassword, sizeof(config.wifiPassword), fallback.wifiPassword);
  copyString(config.barkServerUrl, sizeof(config.barkServerUrl), fallback.barkServerUrl);
  copyString(config.barkDeviceKey, sizeof(config.barkDeviceKey), fallback.barkDeviceKey);
}

void ConfigStoreCore::sanitize(DeviceConfig& config) {
  config.version = kCurrentVersion;
  config.wifiSsid[sizeof(config.wifiSsid) - 1] = '\0';
  config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';
  config.barkServerUrl[sizeof(config.barkServerUrl) - 1] = '\0';
  config.barkDeviceKey[sizeof(config.barkDeviceKey) - 1] = '\0';
}

bool ConfigStoreCore::hasWifiConfig(const DeviceConfig& config) {
  return config.wifiSsid[0] != '\0';
}

bool ConfigStoreCore::hasBarkConfig(const DeviceConfig& config) {
  return config.barkServerUrl[0] != '\0' && config.barkDeviceKey[0] != '\0';
}

void ConfigStoreCore::copyString(char* destination, size_t destinationSize, const char* source) {
  if (destination == nullptr || destinationSize == 0) {
    return;
  }

  destination[0] = '\0';
  if (source == nullptr) {
    return;
  }

  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}
