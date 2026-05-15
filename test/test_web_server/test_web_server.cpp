#include <unity.h>

#include "web_server_core.h"

#include <stdio.h>
#include <string.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

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
  status.cellularSignalKnown = true;
  status.cellularCsq = 20;
  status.cellularRssiDbm = -73;
  status.cellularRegistrationKnown = true;
  status.cellularRegistrationStatus = 1;
  strncpy(status.cellularRegistrationText, "registered_home", sizeof(status.cellularRegistrationText) - 1);
  status.cellularLteSignalKnown = true;
  status.cellularRsrpDbm = -93;
  status.cellularRsrqDbTenths = -25;
  strncpy(status.cellularRsrpQuality, "fair", sizeof(status.cellularRsrpQuality) - 1);
  strncpy(status.cellularCesqRaw, "42,99,255,255,34,47", sizeof(status.cellularCesqRaw) - 1);
  strncpy(status.cellularManufacturer, "CMCC", sizeof(status.cellularManufacturer) - 1);
  strncpy(status.cellularModel, "ML307A", sizeof(status.cellularModel) - 1);
  strncpy(status.cellularFirmware, "ML307A-DSLN-MTSH1S00", sizeof(status.cellularFirmware) - 1);
  strncpy(status.cellularImsi, "001010123456789", sizeof(status.cellularImsi) - 1);
  strncpy(status.cellularIccid, "8901001234567890123F", sizeof(status.cellularIccid) - 1);
  strncpy(status.cellularOwnNumber, "not stored or unsupported", sizeof(status.cellularOwnNumber) - 1);
  strncpy(status.cellularOperatorName, "00101", sizeof(status.cellularOperatorName) - 1);
  status.cellularDataConnectionKnown = true;
  status.cellularDataConnectionActive = true;
  strncpy(status.cellularApn, "EXAMPLE.APN", sizeof(status.cellularApn) - 1);
  status.cellularLastUpdatedMs = 9000;
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

  char output[1536];
  TEST_ASSERT_TRUE(webBuildStatusJson(status, output, sizeof(output)));

  TEST_ASSERT_NOT_NULL(strstr(output, "\"uptime_ms\":12345"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"free_heap\":210000"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"modem\":{\"busy\":true,\"queue_depth\":2}"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"signal_known\":true"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"rssi_dbm\":-73"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"registration\":\"registered_home\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"rsrp_dbm\":-93"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"rsrq_db_tenths\":-25"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"rsrp_quality\":\"fair\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"cesq_raw\":\"42,99,255,255,34,47\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"manufacturer\":\"CMCC\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"model\":\"ML307A\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"firmware\":\"ML307A-DSLN-MTSH1S00\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"imsi\":\"001010123456789\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"iccid\":\"8901001234567890123F\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"own_number\":\"not stored or unsupported\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"operator\":\"00101\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"data_connection_known\":true"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"data_connection_active\":true"));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"apn\":\"EXAMPLE.APN\""));
  TEST_ASSERT_NOT_NULL(strstr(output, "\"last_updated_ms\":9000"));
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

void test_status_page_contains_openwrt_style_dashboard_hooks() {
  char output[6144];

  TEST_ASSERT_TRUE(webBuildPageHtml(WebPageKind::Status, output, sizeof(output)));

  char expectedVersion[32];
  snprintf(expectedVersion, sizeof(expectedVersion), "v%s", APP_VERSION);

  TEST_ASSERT_NOT_NULL(strstr(output, "SMS Bridge"));
  TEST_ASSERT_NOT_NULL(strstr(output, expectedVersion));
  TEST_ASSERT_NOT_NULL(strstr(output, "System"));
  TEST_ASSERT_NOT_NULL(strstr(output, "WiFi"));
  TEST_ASSERT_NOT_NULL(strstr(output, "Modem"));
  TEST_ASSERT_NULL(strstr(output, "4G Modem"));
  TEST_ASSERT_NOT_NULL(strstr(output, "Manufacturer"));
  TEST_ASSERT_NOT_NULL(strstr(output, "Signal RSRP"));
  TEST_ASSERT_NOT_NULL(strstr(output, "signal_icon"));
  TEST_ASSERT_NOT_NULL(strstr(output, "signal-bar"));
  TEST_ASSERT_NOT_NULL(strstr(output, "signal-'+q"));
  TEST_ASSERT_NOT_NULL(strstr(output, "signal-excellent"));
  TEST_ASSERT_NOT_NULL(strstr(output, "signal-unknown"));
  TEST_ASSERT_NOT_NULL(strstr(output, "Last updated"));
  TEST_ASSERT_NOT_NULL(strstr(output, "/api/status"));
}

void test_config_page_posts_json_to_config_api() {
  char output[4096];

  TEST_ASSERT_TRUE(webBuildPageHtml(WebPageKind::Config, output, sizeof(output)));

  TEST_ASSERT_NOT_NULL(strstr(output, "Configuration"));
  TEST_ASSERT_NOT_NULL(strstr(output, "/api/config"));
  TEST_ASSERT_NOT_NULL(strstr(output, "/api/config/save"));
  TEST_ASSERT_NOT_NULL(strstr(output, "wifi_ssid"));
  TEST_ASSERT_NOT_NULL(strstr(output, "bark_device_key"));
  TEST_ASSERT_NOT_NULL(strstr(output, "leave blank to keep"));
  TEST_ASSERT_NOT_NULL(strstr(output, "if($('wifi_password').value)"));
}

void test_queue_and_logs_pages_fit_and_escape_dynamic_rows() {
  char output[4096];

  TEST_ASSERT_TRUE(webBuildPageHtml(WebPageKind::Queue, output, sizeof(output)));
  TEST_ASSERT_NOT_NULL(strstr(output, "Queue"));
  TEST_ASSERT_NOT_NULL(strstr(output, "/api/queue"));
  TEST_ASSERT_NOT_NULL(strstr(output, "function h"));

  TEST_ASSERT_TRUE(webBuildPageHtml(WebPageKind::Logs, output, sizeof(output)));
  TEST_ASSERT_NOT_NULL(strstr(output, "Logs"));
  TEST_ASSERT_NOT_NULL(strstr(output, "/api/logs?limit=80"));
  TEST_ASSERT_NOT_NULL(strstr(output, "function h"));
}

void test_page_html_reports_small_buffer_failure() {
  char output[32];

  TEST_ASSERT_FALSE(webBuildPageHtml(WebPageKind::Status, output, sizeof(output)));
  TEST_ASSERT_EQUAL('\0', output[sizeof(output) - 1]);
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
  RUN_TEST(test_status_page_contains_openwrt_style_dashboard_hooks);
  RUN_TEST(test_config_page_posts_json_to_config_api);
  RUN_TEST(test_queue_and_logs_pages_fit_and_escape_dynamic_rows);
  RUN_TEST(test_page_html_reports_small_buffer_failure);
  return UNITY_END();
}
