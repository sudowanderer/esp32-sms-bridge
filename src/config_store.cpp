#include "config_store.h"

#include "logger.h"

#include <Preferences.h>

#if __has_include("local_wifi_config.h")
#include "local_wifi_config.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

#if __has_include("local_push_config.h")
#include "local_push_config.h"
#else
#define PUSH_BARK_SERVER_URL ""
#define PUSH_BARK_DEVICE_KEY ""
#endif

static constexpr const char* kNamespace = "sms_bridge";
static constexpr const char* kSavedKey = "saved";
static constexpr const char* kVersionKey = "version";
static constexpr const char* kWifiSsidKey = "wifi_ssid";
static constexpr const char* kWifiPasswordKey = "wifi_pass";
static constexpr const char* kBarkServerUrlKey = "bark_url";
static constexpr const char* kBarkDeviceKeyKey = "bark_key";

static Preferences preferences;
static DeviceConfig currentConfig;
static bool preferencesOpen = false;
static bool hasSavedConfig = false;

static DeviceConfigFallback localFallback() {
  DeviceConfigFallback fallback = {};
  fallback.wifiSsid = WIFI_SSID;
  fallback.wifiPassword = WIFI_PASSWORD;
  fallback.barkServerUrl = PUSH_BARK_SERVER_URL;
  fallback.barkDeviceKey = PUSH_BARK_DEVICE_KEY;
  return fallback;
}

static bool ensurePreferencesOpen() {
  if (preferencesOpen) {
    return true;
  }

  preferencesOpen = preferences.begin(kNamespace, false);
  return preferencesOpen;
}

static void readString(const char* key, char* destination, size_t destinationSize) {
  if (destination == nullptr || destinationSize == 0) {
    return;
  }

  destination[0] = '\0';
  preferences.getString(key, destination, destinationSize);
  destination[destinationSize - 1] = '\0';
}

static bool writeString(const char* key, const char* value) {
  if (value == nullptr || value[0] == '\0') {
    preferences.remove(key);
    return true;
  }

  return preferences.putString(key, value) > 0;
}

void configStoreBegin() {
  if (!ensurePreferencesOpen()) {
    ConfigStoreCore::applyFallback(currentConfig, localFallback());
    hasSavedConfig = false;
    logError("config_store_status=preferences_open_failed");
    return;
  }

  if (!configStoreLoad(currentConfig)) {
    ConfigStoreCore::applyFallback(currentConfig, localFallback());
    hasSavedConfig = false;
    logInfo("config_store_status=using_fallback");
    return;
  }

  hasSavedConfig = true;
  logInfo("config_store_status=loaded");
}

bool configStoreLoad(DeviceConfig& config) {
  if (!ensurePreferencesOpen()) {
    return false;
  }

  if (!preferences.getBool(kSavedKey, false)) {
    return false;
  }

  ConfigStoreCore::defaults(config);
  config.version = preferences.getUShort(kVersionKey, ConfigStoreCore::kCurrentVersion);
  readString(kWifiSsidKey, config.wifiSsid, sizeof(config.wifiSsid));
  readString(kWifiPasswordKey, config.wifiPassword, sizeof(config.wifiPassword));
  readString(kBarkServerUrlKey, config.barkServerUrl, sizeof(config.barkServerUrl));
  readString(kBarkDeviceKeyKey, config.barkDeviceKey, sizeof(config.barkDeviceKey));
  ConfigStoreCore::sanitize(config);
  return true;
}

bool configStoreSave(const DeviceConfig& config) {
  if (!ensurePreferencesOpen()) {
    return false;
  }

  DeviceConfig clean = config;
  ConfigStoreCore::sanitize(clean);

  bool ok = true;
  ok = preferences.putUShort(kVersionKey, clean.version) > 0 && ok;
  ok = writeString(kWifiSsidKey, clean.wifiSsid) && ok;
  ok = writeString(kWifiPasswordKey, clean.wifiPassword) && ok;
  ok = writeString(kBarkServerUrlKey, clean.barkServerUrl) && ok;
  ok = writeString(kBarkDeviceKeyKey, clean.barkDeviceKey) && ok;
  ok = preferences.putBool(kSavedKey, true) > 0 && ok;

  if (!ok) {
    logError("config_store_status=save_failed");
    return false;
  }

  currentConfig = clean;
  hasSavedConfig = true;
  logInfo("config_store_status=saved");
  return true;
}

bool configStoreReset() {
  if (!ensurePreferencesOpen()) {
    return false;
  }

  const bool ok = preferences.clear();
  ConfigStoreCore::defaults(currentConfig);
  hasSavedConfig = false;
  logInfo(ok ? "config_store_status=reset" : "config_store_status=reset_failed");
  return ok;
}

const DeviceConfig& configStoreGet() {
  return currentConfig;
}

bool configStoreHasSavedConfig() {
  return hasSavedConfig;
}
