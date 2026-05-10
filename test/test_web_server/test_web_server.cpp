#include <unity.h>

#include "web_server_core.h"

#include <string.h>

void test_json_escape_handles_quotes_backslashes_and_controls() {
  char output[96];

  TEST_ASSERT_TRUE(webJsonEscape("quote\" slash\\ line\n tab\t", output, sizeof(output)));

  TEST_ASSERT_EQUAL_STRING("quote\\\" slash\\\\ line\\n tab\\t", output);
}

void test_json_escape_truncates_safely() {
  char output[8];

  TEST_ASSERT_FALSE(webJsonEscape("abcdefghi", output, sizeof(output)));

  TEST_ASSERT_EQUAL('\0', output[sizeof(output) - 1]);
  TEST_ASSERT_EQUAL_STRING("abcdefg", output);
}

void test_status_json_contains_runtime_state() {
  WebStatusSnapshot status = {};
  status.uptimeMs = 12345;
  status.freeHeap = 210000;
  status.modemBusy = true;
  status.modemQueueDepth = 2;
  status.smsQueueDepth = 3;
  status.smsQueuePending = 1;
  status.wifiConfigured = true;
  status.wifiConnected = true;
  strncpy(status.wifiStatus, "connected", sizeof(status.wifiStatus) - 1);
  strncpy(status.wifiIp, "192.168.1.20", sizeof(status.wifiIp) - 1);
  status.forwarderConfigured = true;
  strncpy(status.forwarderStatus, "last_success", sizeof(status.forwarderStatus) - 1);
  status.forwarderLastCode = 200;
  strncpy(status.forwarderLastError, "", sizeof(status.forwarderLastError) - 1);
  status.loggerCount = 7;

  char output[768];
  TEST_ASSERT_TRUE(webBuildStatusJson(status, output, sizeof(output)));

  TEST_ASSERT_NOT_NULL(strstr(output, "\"uptime_ms\":12345"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"free_heap\":210000"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"modem\":{\"busy\":true,\"queue_depth\":2}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"sms_queue\":{\"depth\":3,\"pending\":1}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"wifi\":{\"configured\":true,\"connected\":true,\"status\":\"connected\",\"ip\":\"192.168.1.20\"}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"forwarder_http\":{\"configured\":true,\"status\":\"last_success\",\"last_code\":200,\"last_error\":\"\"}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"logs\":{\"count\":7}"));
}

void test_log_entry_json_escapes_message() {
  LoggerEntry entry = {};
  entry.timeMs = 1200;
  entry.sequence = 4;
  entry.level = LoggerLevel::Warn;
  strncpy(entry.message, "wifi \"lost\"", sizeof(entry.message) - 1);

  char output[256];
  TEST_ASSERT_TRUE(webBuildLogEntryJson(entry, output, sizeof(output)));

  TEST_ASSERT_EQUAL_STRING("{\"seq\":4,\"time_ms\":1200,\"level\":\"WARN\",\"message\":\"wifi \\\"lost\\\"\"}", output);
}

void test_queue_item_json_contains_forwarding_state_without_sms_body() {
  SmsQueueItem item = {};
  strncpy(item.message.sender, "+8613800138000", sizeof(item.message.sender) - 1);
  strncpy(item.message.timestamp, "260510120000", sizeof(item.message.timestamp) - 1);
  item.status = SmsQueueStatus::Failed;
  item.attemptCount = 2;
  item.createdAtMs = 100;
  item.updatedAtMs = 300;
  item.nextAttemptMs = 1000;
  strncpy(item.lastError, "http_500", sizeof(item.lastError) - 1);

  char output[384];
  TEST_ASSERT_TRUE(webBuildQueueItemJson(item, 1, output, sizeof(output)));

  TEST_ASSERT_NOT_NULL(strstr(output, "\"index\":1"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"sender\":\"+8613800138000\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"timestamp\":\"260510120000\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"status\":\"failed\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"attempts\":2"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"last_error\":\"http_500\""));
  TEST_ASSERT_NULL(strstr(output, "sms_text"));
}

void test_queue_status_names() {
  TEST_ASSERT_EQUAL_STRING("pending", webSmsQueueStatusName(SmsQueueStatus::Pending));
  TEST_ASSERT_EQUAL_STRING("sending", webSmsQueueStatusName(SmsQueueStatus::Sending));
  TEST_ASSERT_EQUAL_STRING("sent", webSmsQueueStatusName(SmsQueueStatus::Sent));
  TEST_ASSERT_EQUAL_STRING("failed", webSmsQueueStatusName(SmsQueueStatus::Failed));
}

void test_config_json_redacts_sensitive_fields() {
  DeviceConfig config = {};
  config.version = ConfigStoreCore::kCurrentVersion;
  strncpy(config.wifiSsid, "Lab \"WiFi\"", sizeof(config.wifiSsid) - 1);
  strncpy(config.wifiPassword, "secret-password", sizeof(config.wifiPassword) - 1);
  strncpy(config.barkServerUrl, "https://api.day.app", sizeof(config.barkServerUrl) - 1);
  strncpy(config.barkDeviceKey, "device-key", sizeof(config.barkDeviceKey) - 1);

  char output[512];
  TEST_ASSERT_TRUE(webBuildConfigJson(config, output, sizeof(output)));

  TEST_ASSERT_NOT_NULL(strstr(output, "\"wifi\":{\"configured\":true,\"ssid\":\"Lab \\\"WiFi\\\"\",\"password_set\":true}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"bark\":{\"configured\":true,\"server_url\":\"https://api.day.app\",\"device_key_set\":true}"));
  TEST_ASSERT_NULL(strstr(output, "secret-password"));
  TEST_ASSERT_NULL(strstr(output, "device-key"));
}

void test_config_json_marks_missing_config() {
  DeviceConfig config = {};
  ConfigStoreCore::defaults(config);

  char output[512];
  TEST_ASSERT_TRUE(webBuildConfigJson(config, output, sizeof(output)));

  TEST_ASSERT_NOT_NULL(strstr(output, "\"version\":1"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"wifi\":{\"configured\":false,\"ssid\":\"\",\"password_set\":false}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"bark\":{\"configured\":false,\"server_url\":\"\",\"device_key_set\":false}"));
}

void test_parse_config_save_json_updates_strings() {
  DeviceConfig config = {};
  ConfigStoreCore::defaults(config);

  WebConfigParseResult result = webParseConfigSaveJson(
      "{\"wifi_ssid\":\"Home\",\"wifi_password\":\"wifi-pass\",\"bark_server_url\":\"https://api.day.app\",\"bark_device_key\":\"key-123\"}",
      config);

  TEST_ASSERT_EQUAL(WebConfigParseResult::Ok, result);
  TEST_ASSERT_EQUAL_STRING("Home", config.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("wifi-pass", config.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("https://api.day.app", config.barkServerUrl);
  TEST_ASSERT_EQUAL_STRING("key-123", config.barkDeviceKey);
}

void test_parse_config_save_json_allows_clearing_fields() {
  DeviceConfig config = {};
  strncpy(config.wifiSsid, "Home", sizeof(config.wifiSsid) - 1);
  strncpy(config.wifiPassword, "wifi-pass", sizeof(config.wifiPassword) - 1);
  strncpy(config.barkServerUrl, "https://api.day.app", sizeof(config.barkServerUrl) - 1);
  strncpy(config.barkDeviceKey, "key-123", sizeof(config.barkDeviceKey) - 1);

  WebConfigParseResult result = webParseConfigSaveJson(
      "{\"wifi_ssid\":\"\",\"wifi_password\":\"\",\"bark_server_url\":\"\",\"bark_device_key\":\"\"}",
      config);

  TEST_ASSERT_EQUAL(WebConfigParseResult::Ok, result);
  TEST_ASSERT_EQUAL_STRING("", config.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("", config.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("", config.barkServerUrl);
  TEST_ASSERT_EQUAL_STRING("", config.barkDeviceKey);
}

void test_parse_config_save_json_rejects_invalid_json() {
  DeviceConfig config = {};
  ConfigStoreCore::defaults(config);

  TEST_ASSERT_EQUAL(WebConfigParseResult::InvalidJson, webParseConfigSaveJson("not-json", config));
  TEST_ASSERT_EQUAL(WebConfigParseResult::InvalidJson, webParseConfigSaveJson("{\"wifi_ssid\":\"Home\"", config));
}

void test_parse_config_save_json_rejects_too_long_value() {
  DeviceConfig config = {};
  ConfigStoreCore::defaults(config);

  TEST_ASSERT_EQUAL(WebConfigParseResult::ValueTooLong,
                    webParseConfigSaveJson("{\"wifi_ssid\":\"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\"}", config));
  TEST_ASSERT_EQUAL_STRING("", config.wifiSsid);
}

void test_parse_config_save_json_decodes_escaped_values() {
  DeviceConfig config = {};
  ConfigStoreCore::defaults(config);

  WebConfigParseResult result = webParseConfigSaveJson(
      "{\"wifi_ssid\":\"Lab \\\"WiFi\\\"\",\"wifi_password\":\"line\\npass\",\"bark_server_url\":\"https:\\/\\/api.day.app\",\"bark_device_key\":\"key\\\\123\"}",
      config);

  TEST_ASSERT_EQUAL(WebConfigParseResult::Ok, result);
  TEST_ASSERT_EQUAL_STRING("Lab \"WiFi\"", config.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("line\npass", config.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("https://api.day.app", config.barkServerUrl);
  TEST_ASSERT_EQUAL_STRING("key\\123", config.barkDeviceKey);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_json_escape_handles_quotes_backslashes_and_controls);
  RUN_TEST(test_json_escape_truncates_safely);
  RUN_TEST(test_status_json_contains_runtime_state);
  RUN_TEST(test_log_entry_json_escapes_message);
  RUN_TEST(test_queue_item_json_contains_forwarding_state_without_sms_body);
  RUN_TEST(test_queue_status_names);
  RUN_TEST(test_config_json_redacts_sensitive_fields);
  RUN_TEST(test_config_json_marks_missing_config);
  RUN_TEST(test_parse_config_save_json_updates_strings);
  RUN_TEST(test_parse_config_save_json_allows_clearing_fields);
  RUN_TEST(test_parse_config_save_json_rejects_invalid_json);
  RUN_TEST(test_parse_config_save_json_rejects_too_long_value);
  RUN_TEST(test_parse_config_save_json_decodes_escaped_values);
  return UNITY_END();
}
