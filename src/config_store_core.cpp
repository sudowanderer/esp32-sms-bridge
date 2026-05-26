#include "config_store_core.h"

#include <ctype.h>
#include <string.h>

void ConfigStoreCore::defaults(DeviceConfig& config) {
  memset(&config, 0, sizeof(config));
  config.version = kCurrentVersion;
}

void ConfigStoreCore::applyFallback(DeviceConfig& config, const DeviceConfigFallback& fallback) {
  defaults(config);
  copyString(config.wifiSsid, sizeof(config.wifiSsid), fallback.wifiSsid);
  copyString(config.wifiPassword, sizeof(config.wifiPassword), fallback.wifiPassword);
  copyString(config.hostname, sizeof(config.hostname), fallback.hostname);
  copyString(config.barkServerUrl, sizeof(config.barkServerUrl), fallback.barkServerUrl);
  copyString(config.barkDeviceKey, sizeof(config.barkDeviceKey), fallback.barkDeviceKey);
}

void ConfigStoreCore::sanitize(DeviceConfig& config) {
  config.version = kCurrentVersion;
  config.wifiSsid[sizeof(config.wifiSsid) - 1] = '\0';
  config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';
  config.hostname[sizeof(config.hostname) - 1] = '\0';
  config.barkServerUrl[sizeof(config.barkServerUrl) - 1] = '\0';
  config.barkDeviceKey[sizeof(config.barkDeviceKey) - 1] = '\0';
  if (!isValidHostname(config.hostname)) {
    config.hostname[0] = '\0';
  }
}

bool ConfigStoreCore::hasWifiConfig(const DeviceConfig& config) {
  return config.wifiSsid[0] != '\0';
}

bool ConfigStoreCore::hasBarkConfig(const DeviceConfig& config) {
  return config.barkServerUrl[0] != '\0' && config.barkDeviceKey[0] != '\0';
}

bool ConfigStoreCore::isValidHostname(const char* hostname) {
  if (hostname == nullptr) {
    return true;
  }

  const size_t length = strlen(hostname);
  if (length == 0) {
    return true;
  }
  if (length > sizeof(DeviceConfig::hostname) - 1) {
    return false;
  }
  if (hostname[0] == '-' || hostname[length - 1] == '-') {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    const unsigned char ch = static_cast<unsigned char>(hostname[i]);
    if (!isalnum(ch) && ch != '-') {
      return false;
    }
  }

  return true;
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
