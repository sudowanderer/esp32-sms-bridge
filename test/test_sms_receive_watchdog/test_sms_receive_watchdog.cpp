#include <unity.h>

#include "modem_commands.h"
#include "sms_receive_watchdog_core.h"

#include <string.h>

void test_waits_until_startup_is_complete() {
  SmsReceiveWatchdogCore core;
  core.begin(100);

  TEST_ASSERT_NULL(core.commandToSubmit(100));

  core.setStartupComplete(true, 200);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(200));
}

void test_configures_cmgf_then_cnmi_then_queries_current_mode() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 10);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(10));
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsDirectUrcMode(), core.commandToSubmit(20));
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, 30);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::querySmsUrcMode(), core.commandToSubmit(30));
  core.markSubmitted(30);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 40);

  TEST_ASSERT_NULL(core.commandToSubmit(30));
  TEST_ASSERT_EQUAL_UINT32(30, core.lastConfigMs());
  TEST_ASSERT_EQUAL_UINT16(1, core.recoveryCount());
}

void test_cnmi_query_response_updates_snapshot_with_description() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);

  const SmsReceiveWatchdogSnapshot snapshot = core.snapshot();
  TEST_ASSERT_TRUE(snapshot.cnmiKnown);
  TEST_ASSERT_EQUAL_STRING("2,2,0,0,0", snapshot.cnmiRaw);
  TEST_ASSERT_NOT_NULL(strstr(snapshot.cnmiDescription, "direct SMS PDU via +CMT"));
  TEST_ASSERT_NOT_NULL(strstr(snapshot.cnmiDescription, "no cell broadcast"));
  TEST_ASSERT_EQUAL_UINT32(30, snapshot.cnmiLastUpdatedMs);
}

void test_failed_cnmi_query_keeps_previous_snapshot_and_completes_cycle() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);

  core.onUrc("+MATREADY", 1000);
  core.markSubmitted(1000);
  core.complete(ModemAtResult::Ok, 1010);
  core.markSubmitted(1010);
  core.complete(ModemAtResult::Ok, 1020);
  core.markSubmitted(1020);
  core.complete(ModemAtResult::Timeout, "", 1030);

  const SmsReceiveWatchdogSnapshot snapshot = core.snapshot();
  TEST_ASSERT_TRUE(snapshot.cnmiKnown);
  TEST_ASSERT_EQUAL_STRING("2,2,0,0,0", snapshot.cnmiRaw);
  TEST_ASSERT_NULL(core.commandToSubmit(1030));
}

void test_status_query_runs_every_30_seconds_without_reapplying_configuration() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);

  TEST_ASSERT_NULL(core.commandToSubmit(30 + SmsReceiveWatchdogCore::kStatusQueryMs - 1));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::querySmsUrcMode(),
                           core.commandToSubmit(30 + SmsReceiveWatchdogCore::kStatusQueryMs));
}

void test_periodic_config_refresh_still_reapplies_sms_configuration() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);
  core.markSubmitted(30 + SmsReceiveWatchdogCore::kStatusQueryMs);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30 + SmsReceiveWatchdogCore::kStatusQueryMs + 10);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(20 + SmsReceiveWatchdogCore::kConfigRefreshMs));
}

void test_failed_status_query_keeps_previous_snapshot_and_waits_for_next_status_query() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);

  core.markSubmitted(30 + SmsReceiveWatchdogCore::kStatusQueryMs);
  core.complete(ModemAtResult::Timeout, "", 30 + SmsReceiveWatchdogCore::kStatusQueryMs + 10);

  const SmsReceiveWatchdogSnapshot snapshot = core.snapshot();
  TEST_ASSERT_TRUE(snapshot.cnmiKnown);
  TEST_ASSERT_EQUAL_STRING("2,2,0,0,0", snapshot.cnmiRaw);
  TEST_ASSERT_NULL(core.commandToSubmit(30 + SmsReceiveWatchdogCore::kStatusQueryMs + 10));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::querySmsUrcMode(),
                           core.commandToSubmit(30 + (2 * SmsReceiveWatchdogCore::kStatusQueryMs) + 10));
}

void test_recovery_urc_during_status_query_runs_config_after_query_finishes() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);

  const uint32_t queryAt = 30 + SmsReceiveWatchdogCore::kStatusQueryMs;
  TEST_ASSERT_EQUAL_STRING(ModemCommands::querySmsUrcMode(), core.commandToSubmit(queryAt));
  core.markSubmitted(queryAt);
  core.onUrc("+CPIN: READY", queryAt + 1);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", queryAt + 10);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(queryAt + 10));
}

void test_matready_and_cpin_ready_trigger_immediate_recovery_after_startup() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 30);

  core.onUrc("+MATREADY", 1000);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(1000));

  core.markSubmitted(1000);
  core.complete(ModemAtResult::Ok, 1010);
  core.markSubmitted(1010);
  core.complete(ModemAtResult::Ok, 1020);
  core.markSubmitted(1020);
  core.complete(ModemAtResult::Ok, "+CNMI: 2,2,0,0,0\n", 1030);

  core.onUrc("+CPIN: READY", 2000);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(2000));
}

void test_failed_command_retries_after_delay_from_first_step() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);

  core.markSubmitted(0);
  core.complete(ModemAtResult::Timeout, 10);

  TEST_ASSERT_NULL(core.commandToSubmit(10 + SmsReceiveWatchdogCore::kRetryDelayMs - 1));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(),
                           core.commandToSubmit(10 + SmsReceiveWatchdogCore::kRetryDelayMs));
}

void test_queue_full_defers_without_marking_pending() {
  SmsReceiveWatchdogCore core;
  core.begin(0);
  core.setStartupComplete(true, 0);

  core.deferSubmission(10);

  TEST_ASSERT_FALSE(core.pending());
  TEST_ASSERT_NULL(core.commandToSubmit(10 + SmsReceiveWatchdogCore::kRetryDelayMs - 1));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(),
                           core.commandToSubmit(10 + SmsReceiveWatchdogCore::kRetryDelayMs));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_waits_until_startup_is_complete);
  RUN_TEST(test_configures_cmgf_then_cnmi_then_queries_current_mode);
  RUN_TEST(test_cnmi_query_response_updates_snapshot_with_description);
  RUN_TEST(test_failed_cnmi_query_keeps_previous_snapshot_and_completes_cycle);
  RUN_TEST(test_status_query_runs_every_30_seconds_without_reapplying_configuration);
  RUN_TEST(test_periodic_config_refresh_still_reapplies_sms_configuration);
  RUN_TEST(test_failed_status_query_keeps_previous_snapshot_and_waits_for_next_status_query);
  RUN_TEST(test_recovery_urc_during_status_query_runs_config_after_query_finishes);
  RUN_TEST(test_matready_and_cpin_ready_trigger_immediate_recovery_after_startup);
  RUN_TEST(test_failed_command_retries_after_delay_from_first_step);
  RUN_TEST(test_queue_full_defers_without_marking_pending);
  return UNITY_END();
}
