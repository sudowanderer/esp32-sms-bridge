#include <unity.h>

#include "modem_events_core.h"

void test_events_start_unknown() {
  ModemEventsCore core;
  core.begin();

  const ModemEventsSnapshot snapshot = core.snapshot();
  TEST_ASSERT_EQUAL(ModemSimState::Unknown, snapshot.simState);
  TEST_ASSERT_EQUAL_UINT16(0, snapshot.matreadyCount);
  TEST_ASSERT_EQUAL_UINT16(0, snapshot.cpinReadyCount);
  TEST_ASSERT_EQUAL_UINT16(0, snapshot.simRemovedCount);
}

void test_matready_is_counted_each_time() {
  ModemEventsCore core;
  core.begin();

  TEST_ASSERT_TRUE(core.onUrc("+MATREADY", 1000));
  TEST_ASSERT_TRUE(core.onUrc("+MATREADY", 2500));

  const ModemEventsSnapshot snapshot = core.snapshot();
  TEST_ASSERT_EQUAL_UINT16(2, snapshot.matreadyCount);
  TEST_ASSERT_EQUAL_UINT32(2500, snapshot.lastMatreadyMs);
  TEST_ASSERT_EQUAL_STRING("matready", snapshot.lastEvent);
}

void test_cpin_ready_then_removed_within_pair_window_keeps_ready_state() {
  ModemEventsCore core;
  core.begin();

  TEST_ASSERT_TRUE(core.onUrc("+CPIN: READY", 100));
  ModemEventsSnapshot snapshot = core.snapshot();
  TEST_ASSERT_EQUAL(ModemSimState::Ready, snapshot.simState);
  TEST_ASSERT_EQUAL_UINT16(1, snapshot.cpinReadyCount);
  TEST_ASSERT_EQUAL_UINT32(100, snapshot.lastCpinReadyMs);
  TEST_ASSERT_EQUAL_STRING("cpin_ready", snapshot.lastEvent);

  TEST_ASSERT_TRUE(core.onUrc("+CPIN: SIM REMOVED", 300));
  snapshot = core.snapshot();
  TEST_ASSERT_EQUAL(ModemSimState::Ready, snapshot.simState);
  TEST_ASSERT_EQUAL_UINT16(1, snapshot.simRemovedCount);
  TEST_ASSERT_EQUAL_UINT32(300, snapshot.lastSimRemovedMs);
  TEST_ASSERT_EQUAL_STRING("sim_removed", snapshot.lastEvent);
}

void test_removed_then_ready_within_pair_window_recovers_ready_state() {
  ModemEventsCore core;
  core.begin();

  TEST_ASSERT_TRUE(core.onUrc("+CPIN: SIM REMOVED", 100));
  ModemEventsSnapshot snapshot = core.snapshot();
  TEST_ASSERT_EQUAL(ModemSimState::SuspectRemoved, snapshot.simState);
  TEST_ASSERT_EQUAL_STRING("sim_removed", snapshot.lastEvent);

  TEST_ASSERT_TRUE(core.onUrc("+CPIN: READY", 300));
  snapshot = core.snapshot();
  TEST_ASSERT_EQUAL(ModemSimState::Ready, snapshot.simState);
  TEST_ASSERT_EQUAL_UINT16(1, snapshot.cpinReadyCount);
  TEST_ASSERT_EQUAL_UINT16(1, snapshot.simRemovedCount);
  TEST_ASSERT_EQUAL_STRING("cpin_ready", snapshot.lastEvent);
}

void test_removed_without_ready_becomes_removed_after_pair_window() {
  ModemEventsCore core;
  core.begin();

  TEST_ASSERT_TRUE(core.onUrc("+CPIN: SIM REMOVED", 100));
  core.poll(100 + ModemEventsCore::kCpinPairWindowMs - 1);
  TEST_ASSERT_EQUAL(ModemSimState::SuspectRemoved, core.snapshot().simState);

  core.poll(100 + ModemEventsCore::kCpinPairWindowMs);
  TEST_ASSERT_EQUAL(ModemSimState::Removed, core.snapshot().simState);
}

void test_old_ready_does_not_pair_with_late_removed() {
  ModemEventsCore core;
  core.begin();

  TEST_ASSERT_TRUE(core.onUrc("+CPIN: READY", 100));
  TEST_ASSERT_TRUE(core.onUrc("+CPIN: SIM REMOVED", 100 + ModemEventsCore::kCpinPairWindowMs + 1));

  TEST_ASSERT_EQUAL(ModemSimState::SuspectRemoved, core.snapshot().simState);
}

void test_unrelated_urc_is_ignored() {
  ModemEventsCore core;
  core.begin();

  TEST_ASSERT_FALSE(core.onUrc("+CEREG: 0,1", 100));

  const ModemEventsSnapshot snapshot = core.snapshot();
  TEST_ASSERT_EQUAL(ModemSimState::Unknown, snapshot.simState);
  TEST_ASSERT_EQUAL_UINT16(0, snapshot.matreadyCount);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_events_start_unknown);
  RUN_TEST(test_matready_is_counted_each_time);
  RUN_TEST(test_cpin_ready_then_removed_within_pair_window_keeps_ready_state);
  RUN_TEST(test_removed_then_ready_within_pair_window_recovers_ready_state);
  RUN_TEST(test_removed_without_ready_becomes_removed_after_pair_window);
  RUN_TEST(test_old_ready_does_not_pair_with_late_removed);
  RUN_TEST(test_unrelated_urc_is_ignored);
  return UNITY_END();
}
