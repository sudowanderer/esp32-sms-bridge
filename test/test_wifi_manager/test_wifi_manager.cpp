#include <unity.h>

#include "wifi_manager_core.h"

void test_unconfigured_wifi_does_not_request_connection() {
  WifiManagerCore wifi;

  wifi.begin(false, 100);

  TEST_ASSERT_EQUAL(WifiManagerStatus::Unconfigured, wifi.status());
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());
}

void test_configured_wifi_requests_initial_connection() {
  WifiManagerCore wifi;

  wifi.begin(true, 100);

  TEST_ASSERT_EQUAL(WifiManagerStatus::Connecting, wifi.status());
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());
}

void test_connected_status_tracks_successful_station_connection() {
  WifiManagerCore wifi;
  wifi.begin(true, 100);
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());

  wifi.poll(true, 250);

  TEST_ASSERT_EQUAL(WifiManagerStatus::Connected, wifi.status());
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());
}

void test_disconnect_waits_for_retry_interval_before_reconnecting() {
  WifiManagerCore wifi;
  wifi.begin(true, 100);
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());
  wifi.poll(true, 200);

  wifi.poll(false, 300);
  TEST_ASSERT_EQUAL(WifiManagerStatus::Disconnected, wifi.status());
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());

  wifi.poll(false, 300 + WifiManagerCore::kReconnectIntervalMs - 1);
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());

  wifi.poll(false, 300 + WifiManagerCore::kReconnectIntervalMs);
  TEST_ASSERT_EQUAL(WifiManagerStatus::Connecting, wifi.status());
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());
}

void test_failed_connecting_state_retries_on_interval() {
  WifiManagerCore wifi;
  wifi.begin(true, 100);
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());

  wifi.poll(false, 100 + WifiManagerCore::kReconnectIntervalMs - 1);
  TEST_ASSERT_EQUAL(WifiManagerStatus::Connecting, wifi.status());
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());

  wifi.poll(false, 100 + WifiManagerCore::kReconnectIntervalMs);
  TEST_ASSERT_EQUAL(WifiManagerStatus::Connecting, wifi.status());
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());
}

void test_manual_reconnect_requests_immediate_connection() {
  WifiManagerCore wifi;
  wifi.begin(true, 100);
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());
  wifi.poll(true, 200);
  TEST_ASSERT_EQUAL(WifiManagerStatus::Connected, wifi.status());

  wifi.requestReconnect(true, 500);

  TEST_ASSERT_EQUAL(WifiManagerStatus::Connecting, wifi.status());
  TEST_ASSERT_TRUE(wifi.consumeConnectRequest());
  TEST_ASSERT_EQUAL_UINT32(500, wifi.lastConnectAttemptMs());
}

void test_manual_reconnect_without_credentials_becomes_unconfigured() {
  WifiManagerCore wifi;
  wifi.begin(true, 100);

  wifi.requestReconnect(false, 500);

  TEST_ASSERT_EQUAL(WifiManagerStatus::Unconfigured, wifi.status());
  TEST_ASSERT_FALSE(wifi.consumeConnectRequest());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_unconfigured_wifi_does_not_request_connection);
  RUN_TEST(test_configured_wifi_requests_initial_connection);
  RUN_TEST(test_connected_status_tracks_successful_station_connection);
  RUN_TEST(test_disconnect_waits_for_retry_interval_before_reconnecting);
  RUN_TEST(test_failed_connecting_state_retries_on_interval);
  RUN_TEST(test_manual_reconnect_requests_immediate_connection);
  RUN_TEST(test_manual_reconnect_without_credentials_becomes_unconfigured);
  return UNITY_END();
}
