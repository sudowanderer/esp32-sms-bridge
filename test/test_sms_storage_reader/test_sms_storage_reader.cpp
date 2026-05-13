#include <unity.h>

#include "sms_storage_reader_core.h"

#include <string.h>

struct StatusContext {
  SmsStorageReaderStatus last;
  int count;
};

static void captureStatus(const SmsStorageReaderStatus& status, void* userData) {
  StatusContext* ctx = static_cast<StatusContext*>(userData);
  ctx->last = status;
  ctx->count++;
}

static SmsStorageNotification notification(const char* storage, uint16_t index) {
  SmsStorageNotification n = {};
  strncpy(n.storage, storage, sizeof(n.storage) - 1);
  n.index = index;
  return n;
}

void test_enqueued_cmti_generates_cmgr_command() {
  SmsStorageReaderCore core;
  StatusContext ctx = {};
  core.begin();
  core.setStatusCallback(captureStatus, &ctx);

  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 37)));

  char command[32];
  TEST_ASSERT_TRUE(core.nextReadCommand(command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT+CMGR=37", command);
  TEST_ASSERT_TRUE(core.isActive());
  TEST_ASSERT_EQUAL(0, core.queuedCount());
  TEST_ASSERT_EQUAL(SmsStorageReaderEvent::ReadCommandReady, ctx.last.event);
}

void test_cmgr_response_extracts_first_hex_pdu_line() {
  SmsStorageReaderCore core;
  core.begin();
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 37)));

  char command[32];
  TEST_ASSERT_TRUE(core.nextReadCommand(command, sizeof(command)));

  char pdu[64];
  TEST_ASSERT_TRUE(core.completeRead(ModemAtResult::Ok,
                                    "+CMGR: 1,,24\n0891683108200505F0\n",
                                    pdu,
                                    sizeof(pdu)));
  TEST_ASSERT_EQUAL_STRING("0891683108200505F0", pdu);
  TEST_ASSERT_TRUE(core.isActive());
}

void test_successfully_queued_message_generates_delete_command() {
  SmsStorageReaderCore core;
  core.begin();
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 37)));

  char command[32];
  char pdu[64];
  TEST_ASSERT_TRUE(core.nextReadCommand(command, sizeof(command)));
  TEST_ASSERT_TRUE(core.completeRead(ModemAtResult::Ok, "+CMGR: 1,,24\n0891683108200505F0\n", pdu, sizeof(pdu)));

  TEST_ASSERT_TRUE(core.messageQueued(command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT+CMGD=37", command);
  TEST_ASSERT_TRUE(core.isActive());

  core.completeDelete(ModemAtResult::Ok);
  TEST_ASSERT_FALSE(core.isActive());
}

void test_read_error_clears_active_without_delete() {
  SmsStorageReaderCore core;
  StatusContext ctx = {};
  core.begin();
  core.setStatusCallback(captureStatus, &ctx);
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 37)));

  char command[32];
  char pdu[64];
  TEST_ASSERT_TRUE(core.nextReadCommand(command, sizeof(command)));
  TEST_ASSERT_FALSE(core.completeRead(ModemAtResult::Timeout, "", pdu, sizeof(pdu)));
  TEST_ASSERT_FALSE(core.isActive());
  TEST_ASSERT_EQUAL(SmsStorageReaderEvent::ReadFailed, ctx.last.event);

  TEST_ASSERT_FALSE(core.messageQueued(command, sizeof(command)));
}

void test_non_hex_cmgr_response_is_read_failure() {
  SmsStorageReaderCore core;
  StatusContext ctx = {};
  core.begin();
  core.setStatusCallback(captureStatus, &ctx);
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 37)));

  char command[32];
  char pdu[64];
  TEST_ASSERT_TRUE(core.nextReadCommand(command, sizeof(command)));
  TEST_ASSERT_FALSE(core.completeRead(ModemAtResult::Ok, "+CMGR: 1,,24\nnot-a-pdu\n", pdu, sizeof(pdu)));
  TEST_ASSERT_FALSE(core.isActive());
  TEST_ASSERT_EQUAL(SmsStorageReaderEvent::ReadFailed, ctx.last.event);
  TEST_ASSERT_EQUAL_STRING("pdu_missing", ctx.last.detail);
}

void test_queue_full_is_reported() {
  SmsStorageReaderCore core;
  StatusContext ctx = {};
  core.begin();
  core.setStatusCallback(captureStatus, &ctx);

  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 1)));
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 2)));
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 3)));
  TEST_ASSERT_TRUE(core.enqueue(notification("SM", 4)));
  TEST_ASSERT_FALSE(core.enqueue(notification("SM", 5)));
  TEST_ASSERT_EQUAL(SmsStorageReaderEvent::QueueFull, ctx.last.event);
  TEST_ASSERT_EQUAL(5, ctx.last.index);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_enqueued_cmti_generates_cmgr_command);
  RUN_TEST(test_cmgr_response_extracts_first_hex_pdu_line);
  RUN_TEST(test_successfully_queued_message_generates_delete_command);
  RUN_TEST(test_read_error_clears_active_without_delete);
  RUN_TEST(test_non_hex_cmgr_response_is_read_failure);
  RUN_TEST(test_queue_full_is_reported);
  return UNITY_END();
}
