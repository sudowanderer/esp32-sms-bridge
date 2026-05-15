#include <unity.h>

#include "modem_commands.h"
#include "pdp_guard_core.h"

static const char* kContexts =
    "+CGDCONT: 1,\"IP\",\"GIFFGAFF.COM\",,0,0,,,,\n"
    "+CGDCONT: 8,\"IPV4V6\",\"IMS\",,0,0,0,2,1,1\n";

void test_guard_queries_contexts_after_startup_complete() {
  PdpGuardCore guard;
  guard.begin();

  TEST_ASSERT_NULL(guard.commandToSubmit(0));
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
}

void test_guard_queries_activation_after_contexts() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, kContexts, 110);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpActivation(), guard.commandToSubmit(110));
}

void test_guard_deactivates_active_non_ims_context_only() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, kContexts, 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpActivation(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+CGACT: 1,1\n+CGACT: 8,1\nOK\n", 120);

  TEST_ASSERT_EQUAL_UINT8(1, guard.lastTargetCid());
  TEST_ASSERT_EQUAL_STRING("AT+CGACT=0,1", guard.commandToSubmit(120));
}

void test_guard_ignores_ims_only_active_context() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, kContexts, 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpActivation(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+CGACT: 1,0\n+CGACT: 8,1\nOK\n", 120);

  TEST_ASSERT_TRUE(guard.isDeactivated());
  TEST_ASSERT_TRUE(guard.hasOnlyIgnoredContextsActive());
  TEST_ASSERT_NULL(guard.commandToSubmit(120));
}

void test_guard_stops_when_all_contexts_inactive() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, kContexts, 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpActivation(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+CGACT: 1,0\n+CGACT: 8,0\nOK\n", 120);

  TEST_ASSERT_TRUE(guard.isDeactivated());
  TEST_ASSERT_FALSE(guard.hasOnlyIgnoredContextsActive());
  TEST_ASSERT_NULL(guard.commandToSubmit(120));
}

void test_deactivate_success_rechecks_context_state() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, kContexts, 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpActivation(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+CGACT: 1,1\n+CGACT: 8,1\nOK\n", 120);
  TEST_ASSERT_EQUAL_STRING("AT+CGACT=0,1", guard.commandToSubmit(120));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "OK\n", 130);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(130));
}

void test_deactivate_failure_retries_from_context_query_later() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, kContexts, 110);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpActivation(), guard.commandToSubmit(110));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Ok, "+CGACT: 1,1\n+CGACT: 8,1\nOK\n", 120);
  TEST_ASSERT_EQUAL_STRING("AT+CGACT=0,1", guard.commandToSubmit(120));
  guard.markSubmitted();
  guard.complete(ModemAtResult::Error, "+CME ERROR: 100\n", 130);

  TEST_ASSERT_NULL(guard.commandToSubmit(60129));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(60130));
}

void test_queue_full_defers_guard_attempt() {
  PdpGuardCore guard;
  guard.begin();
  guard.setStartupComplete(true, 100);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(100));
  guard.deferSubmission(100);

  TEST_ASSERT_NULL(guard.commandToSubmit(60099));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryPdpContext(), guard.commandToSubmit(60100));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_guard_queries_contexts_after_startup_complete);
  RUN_TEST(test_guard_queries_activation_after_contexts);
  RUN_TEST(test_guard_deactivates_active_non_ims_context_only);
  RUN_TEST(test_guard_ignores_ims_only_active_context);
  RUN_TEST(test_guard_stops_when_all_contexts_inactive);
  RUN_TEST(test_deactivate_success_rechecks_context_state);
  RUN_TEST(test_deactivate_failure_retries_from_context_query_later);
  RUN_TEST(test_queue_full_defers_guard_attempt);
  return UNITY_END();
}
