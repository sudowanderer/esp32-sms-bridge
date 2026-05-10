#pragma once

#include <stddef.h>
#include <stdint.h>

struct DeviceConfig {
  uint16_t version;
  char wifiSsid[64];
  char wifiPassword[96];
  char barkServerUrl[128];
  char barkDeviceKey[96];
};

struct DeviceConfigFallback {
  const char* wifiSsid;
  const char* wifiPassword;
  const char* barkServerUrl;
  const char* barkDeviceKey;
};

class ConfigStoreCore {
 public:
  static constexpr uint16_t kCurrentVersion = 1;

  static void defaults(DeviceConfig& config);
  static void applyFallback(DeviceConfig& config, const DeviceConfigFallback& fallback);
  static void sanitize(DeviceConfig& config);
  static bool hasWifiConfig(const DeviceConfig& config);
  static bool hasBarkConfig(const DeviceConfig& config);
  static void copyString(char* destination, size_t destinationSize, const char* source);
};
