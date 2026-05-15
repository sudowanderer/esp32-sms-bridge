#include <unity.h>

#include "modem_commands.h"
#include "pdp_guard_core.h"

void test_guard_queries_mipcall_after_startup_complete() {
  PdpGuardCore guard;
  guard.begin();

  TEST_ASSERT_NULL(guard.commandToSubmit(0));
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
}

void test_guard_disconnects_when_mipcall_is_active() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+MIPCALL: 1,1,\"10.64.10.190\"\nOK\n", 110);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::disconnectMipCall(), guard.commandToSubmit(110));
}

void test_guard_stops_when_mipcall_is_already_disconnected() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+MIPCALL: 1,0\nOK\n", 110);

  TEST_ASSERT_TRUE(guard.isDeactivated());
  TEST_ASSERT_TRUE(guard.isAlreadyDisconnected());
  TEST_ASSERT_NULL(guard.commandToSubmit(110));
}

void test_disconnect_success_completes_guard() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+MIPCALL: 1,1,\"10.64.10.190\"\nOK\n", 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::disconnectMipCall(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "OK\n+MIPCALL:1,0\n", 120);

  TEST_ASSERT_TRUE(guard.isDeactivated());
  TEST_ASSERT_FALSE(guard.isAlreadyDisconnected());
  TEST_ASSERT_NULL(guard.commandToSubmit(120));
}

void test_query_failure_retries_later() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Error, "ERROR\n", 110);

  TEST_ASSERT_NULL(guard.commandToSubmit(60109));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(60110));
}

void test_disconnect_failure_retries_from_query_later() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+MIPCALL: 1,1,\"10.64.10.190\"\nOK\n", 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::disconnectMipCall(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Error, "+CME ERROR: 100\n", 120);

  TEST_ASSERT_NULL(guard.commandToSubmit(60119));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(60120));
}

void test_queue_full_defers_guard_attempt() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(100));
  guard.deferSubmission(100);

  TEST_ASSERT_NULL(guard.commandToSubmit(60099));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryMipCall(), guard.commandToSubmit(60100));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_guard_queries_mipcall_after_startup_complete);
  RUN_TEST(test_guard_disconnects_when_mipcall_is_active);
  RUN_TEST(test_guard_stops_when_mipcall_is_already_disconnected);
  RUN_TEST(test_disconnect_success_completes_guard);
  RUN_TEST(test_query_failure_retries_later);
  RUN_TEST(test_disconnect_failure_retries_from_query_later);
  RUN_TEST(test_queue_full_defers_guard_attempt);
  return UNITY_END();
}
