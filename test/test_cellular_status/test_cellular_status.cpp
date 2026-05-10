#include <unity.h>

#include "cellular_status_core.h"

#include <string.h>

void test_parse_csq_response_sets_signal_strength() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCsqResponse("+CSQ: 20,99\r\n", status, 1234));

  TEST_ASSERT_TRUE(status.signalKnown);
  TEST_ASSERT_EQUAL_UINT8(20, status.csqRssi);
  TEST_ASSERT_EQUAL_INT16(-73, status.rssiDbm);
  TEST_ASSERT_EQUAL_UINT8(99, status.csqBer);
  TEST_ASSERT_EQUAL_UINT32(1234, status.lastUpdatedMs);
}

void test_parse_csq_response_marks_unknown_signal() {
  CellularStatusSnapshot status = {};
  status.signalKnown = true;
  status.csqRssi = 15;
  status.rssiDbm = -83;

  TEST_ASSERT_TRUE(CellularStatusCore::parseCsqResponse("+CSQ: 99,99", status, 2000));

  TEST_ASSERT_FALSE(status.signalKnown);
  TEST_ASSERT_EQUAL_UINT8(99, status.csqRssi);
  TEST_ASSERT_EQUAL_INT16(0, status.rssiDbm);
  TEST_ASSERT_EQUAL_UINT32(2000, status.lastUpdatedMs);
}

void test_parse_csq_response_rejects_invalid_text() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_FALSE(CellularStatusCore::parseCsqResponse("OK", status, 100));
  TEST_ASSERT_FALSE(status.signalKnown);
}

void test_parse_cereg_response_sets_registered_home() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCeregResponse("+CEREG: 0,1\r\n", status, 3000));

  TEST_ASSERT_TRUE(status.registrationKnown);
  TEST_ASSERT_EQUAL_UINT8(1, status.registrationStatus);
  TEST_ASSERT_EQUAL_STRING("registered_home", status.registrationText);
  TEST_ASSERT_EQUAL_UINT32(3000, status.lastUpdatedMs);
}

void test_parse_cereg_response_sets_roaming() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCeregResponse("+CEREG: 0,5", status, 3000));

  TEST_ASSERT_TRUE(status.registrationKnown);
  TEST_ASSERT_EQUAL_UINT8(5, status.registrationStatus);
  TEST_ASSERT_EQUAL_STRING("registered_roaming", status.registrationText);
}

void test_parse_cereg_response_rejects_invalid_text() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_FALSE(CellularStatusCore::parseCeregResponse("+CSQ: 20,99", status, 100));
  TEST_ASSERT_FALSE(status.registrationKnown);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_parse_csq_response_sets_signal_strength);
  RUN_TEST(test_parse_csq_response_marks_unknown_signal);
  RUN_TEST(test_parse_csq_response_rejects_invalid_text);
  RUN_TEST(test_parse_cereg_response_sets_registered_home);
  RUN_TEST(test_parse_cereg_response_sets_roaming);
  RUN_TEST(test_parse_cereg_response_rejects_invalid_text);
  return UNITY_END();
}
