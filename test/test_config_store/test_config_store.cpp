#include <unity.h>

#include "config_store_core.h"

#include <string.h>

void test_default_config_is_empty_and_versioned() {
  DeviceConfig config;

  ConfigStoreCore::defaults(config);

  TEST_ASSERT_EQUAL(ConfigStoreCore::kCurrentVersion, config.version);
  TEST_ASSERT_EQUAL_STRING("", config.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("", config.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("", config.barkServerUrl);
  TEST_ASSERT_EQUAL_STRING("", config.barkDeviceKey);
  TEST_ASSERT_FALSE(ConfigStoreCore::hasWifiConfig(config));
  TEST_ASSERT_FALSE(ConfigStoreCore::hasBarkConfig(config));
}

void test_fallback_populates_wifi_and_bark_config() {
  DeviceConfig config;
  DeviceConfigFallback fallback = {};
  fallback.wifiSsid = "LabWiFi";
  fallback.wifiPassword = "wifi-password";
  fallback.barkServerUrl = "https://api.day.app";
  fallback.barkDeviceKey = "device-key";

  ConfigStoreCore::applyFallback(config, fallback);

  TEST_ASSERT_EQUAL_STRING("LabWiFi", config.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("wifi-password", config.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("https://api.day.app", config.barkServerUrl);
  TEST_ASSERT_EQUAL_STRING("device-key", config.barkDeviceKey);
  TEST_ASSERT_TRUE(ConfigStoreCore::hasWifiConfig(config));
  TEST_ASSERT_TRUE(ConfigStoreCore::hasBarkConfig(config));
}

void test_missing_bark_device_key_is_unconfigured() {
  DeviceConfig config;
  ConfigStoreCore::defaults(config);
  ConfigStoreCore::copyString(config.barkServerUrl, sizeof(config.barkServerUrl), "https://api.day.app");

  TEST_ASSERT_FALSE(ConfigStoreCore::hasBarkConfig(config));
}

void test_copy_string_truncates_and_null_terminates() {
  char output[6];

  ConfigStoreCore::copyString(output, sizeof(output), "abcdefg");

  TEST_ASSERT_EQUAL_STRING("abcde", output);
  TEST_ASSERT_EQUAL('\0', output[sizeof(output) - 1]);
}

void test_sanitize_restores_version_and_string_termination() {
  DeviceConfig config;
  memset(&config, 'x', sizeof(config));
  config.version = 99;

  ConfigStoreCore::sanitize(config);

  TEST_ASSERT_EQUAL(ConfigStoreCore::kCurrentVersion, config.version);
  TEST_ASSERT_EQUAL('\0', config.wifiSsid[sizeof(config.wifiSsid) - 1]);
  TEST_ASSERT_EQUAL('\0', config.wifiPassword[sizeof(config.wifiPassword) - 1]);
  TEST_ASSERT_EQUAL('\0', config.barkServerUrl[sizeof(config.barkServerUrl) - 1]);
  TEST_ASSERT_EQUAL('\0', config.barkDeviceKey[sizeof(config.barkDeviceKey) - 1]);
}

void test_config_struct_can_round_trip_after_sanitize() {
  DeviceConfig saved;
  ConfigStoreCore::defaults(saved);
  ConfigStoreCore::copyString(saved.wifiSsid, sizeof(saved.wifiSsid), "O2");
  ConfigStoreCore::copyString(saved.wifiPassword, sizeof(saved.wifiPassword), "secret");
  ConfigStoreCore::copyString(saved.barkServerUrl, sizeof(saved.barkServerUrl), "https://api.day.app");
  ConfigStoreCore::copyString(saved.barkDeviceKey, sizeof(saved.barkDeviceKey), "device-key");

  DeviceConfig loaded = saved;
  ConfigStoreCore::sanitize(loaded);

  TEST_ASSERT_EQUAL_MEMORY(&saved, &loaded, sizeof(DeviceConfig));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_default_config_is_empty_and_versioned);
  RUN_TEST(test_fallback_populates_wifi_and_bark_config);
  RUN_TEST(test_missing_bark_device_key_is_unconfigured);
  RUN_TEST(test_copy_string_truncates_and_null_terminates);
  RUN_TEST(test_sanitize_restores_version_and_string_termination);
  RUN_TEST(test_config_struct_can_round_trip_after_sanitize);
  return UNITY_END();
}
