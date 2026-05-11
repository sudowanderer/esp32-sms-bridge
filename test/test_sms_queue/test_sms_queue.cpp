#include <unity.h>

#include "sms_queue_core.h"

#include <stdio.h>
#include <string.h>

static void fillMessage(SmsMessage& message, const char* sender, const char* text) {
  memset(&message, 0, sizeof(message));
  strncpy(message.sender, sender, sizeof(message.sender) - 1);
  strncpy(message.timestamp, "260510120000", sizeof(message.timestamp) - 1);
  strncpy(message.text, text, sizeof(message.text) - 1);
  strncpy(message.pdu, "0891683108200505F0", sizeof(message.pdu) - 1);
}

void test_queue_starts_empty() {
  SmsQueueCore queue;
  queue.begin();

  TEST_ASSERT_EQUAL(0, queue.depth());
  TEST_ASSERT_EQUAL(0, queue.pendingCount());
  TEST_ASSERT_EQUAL(SmsQueueCore::kCapacity, queue.capacity());
  TEST_ASSERT_NULL(queue.get(0));
}

void test_enqueue_adds_pending_sms() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");

  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  TEST_ASSERT_EQUAL(1, queue.depth());
  TEST_ASSERT_EQUAL(1, queue.pendingCount());
  const SmsQueueItem* item = queue.get(0);
  TEST_ASSERT_NOT_NULL(item);
  TEST_ASSERT_EQUAL(SmsQueueStatus::Pending, item->status);
  TEST_ASSERT_EQUAL(0, item->attemptCount);
  TEST_ASSERT_EQUAL(100, item->createdAtMs);
  TEST_ASSERT_EQUAL(100, item->updatedAtMs);
  TEST_ASSERT_EQUAL(100, item->nextAttemptMs);
  TEST_ASSERT_EQUAL_STRING("+8613800138000", item->message.sender);
  TEST_ASSERT_EQUAL_STRING("hello", item->message.text);
  TEST_ASSERT_EQUAL_STRING("", item->lastError);
}

void test_full_queue_rejects_new_sms_without_overwriting_existing_items() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;

  for (uint8_t i = 0; i < SmsQueueCore::kCapacity; ++i) {
    char sender[32];
    snprintf(sender, sizeof(sender), "+86138001380%02u", i);
    fillMessage(message, sender, "queued");
    TEST_ASSERT_TRUE(queue.enqueue(message, 100 + i));
  }

  fillMessage(message, "+8699999999999", "overflow");
  TEST_ASSERT_FALSE(queue.enqueue(message, 999));

  TEST_ASSERT_EQUAL(SmsQueueCore::kCapacity, queue.depth());
  TEST_ASSERT_EQUAL_STRING("+8613800138000", queue.get(0)->message.sender);
  TEST_ASSERT_EQUAL_STRING("+8613800138007", queue.get(SmsQueueCore::kCapacity - 1)->message.sender);
}

void test_acquire_next_returns_due_pending_sms_only() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage first;
  SmsMessage second;
  fillMessage(first, "+8613800138000", "first");
  fillMessage(second, "+8613800138001", "second");

  TEST_ASSERT_TRUE(queue.enqueue(first, 100));
  TEST_ASSERT_TRUE(queue.enqueue(second, 200));
  SmsQueueItem* item = queue.acquireNext(100);
  TEST_ASSERT_NOT_NULL(item);
  queue.markFailed(item, "http_timeout", 500, 100);

  item = queue.acquireNext(200);
  TEST_ASSERT_NOT_NULL(item);
  TEST_ASSERT_EQUAL_STRING("second", item->message.text);

  queue.markSending(item, 201);
  TEST_ASSERT_NULL(queue.acquireNext(400));
  TEST_ASSERT_NOT_NULL(queue.acquireNext(600));
  TEST_ASSERT_EQUAL_STRING("first", queue.acquireNext(600)->message.text);
}

void test_mark_sending_and_sent_removes_item_from_queue() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  SmsQueueItem* item = queue.acquireNext(100);
  TEST_ASSERT_NOT_NULL(item);
  queue.markSending(item, 101);

  TEST_ASSERT_EQUAL(SmsQueueStatus::Sending, item->status);
  TEST_ASSERT_EQUAL(101, item->updatedAtMs);
  TEST_ASSERT_NULL(queue.acquireNext(102));

  queue.markSent(item, 120);
  TEST_ASSERT_NULL(queue.acquireNext(200));
  TEST_ASSERT_EQUAL(0, queue.pendingCount());
  TEST_ASSERT_EQUAL(0, queue.depth());
  TEST_ASSERT_NULL(queue.get(0));
}

void test_mark_sent_frees_capacity_for_new_sms() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;

  for (uint8_t i = 0; i < SmsQueueCore::kCapacity; ++i) {
    char sender[32];
    snprintf(sender, sizeof(sender), "+86138001380%02u", i);
    fillMessage(message, sender, "queued");
    TEST_ASSERT_TRUE(queue.enqueue(message, 100 + i));
  }

  SmsQueueItem* item = queue.acquireNext(200);
  TEST_ASSERT_NOT_NULL(item);
  queue.markSent(item, 250);

  fillMessage(message, "+8699999999999", "after sent");
  TEST_ASSERT_TRUE(queue.enqueue(message, 300));

  TEST_ASSERT_EQUAL(SmsQueueCore::kCapacity, queue.depth());
  TEST_ASSERT_EQUAL_STRING("+8613800138001", queue.get(0)->message.sender);
  TEST_ASSERT_EQUAL_STRING("+8699999999999", queue.get(SmsQueueCore::kCapacity - 1)->message.sender);
}

void test_mark_failed_sets_retry_metadata_and_truncates_error() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  SmsQueueItem* item = queue.acquireNext(100);
  TEST_ASSERT_NOT_NULL(item);
  queue.markFailed(item,
                   "this_error_message_is_intentionally_long_to_verify_that_the_queue_keeps_a_safe_null_terminated_copy",
                   3000,
                   150);

  TEST_ASSERT_EQUAL(SmsQueueStatus::Failed, item->status);
  TEST_ASSERT_EQUAL(1, item->attemptCount);
  TEST_ASSERT_EQUAL(3150, item->nextAttemptMs);
  TEST_ASSERT_EQUAL(150, item->updatedAtMs);
  TEST_ASSERT_EQUAL('\0', item->lastError[sizeof(item->lastError) - 1]);
  TEST_ASSERT_EQUAL(0, strncmp("this_error_message", item->lastError, strlen("this_error_message")));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_queue_starts_empty);
  RUN_TEST(test_enqueue_adds_pending_sms);
  RUN_TEST(test_full_queue_rejects_new_sms_without_overwriting_existing_items);
  RUN_TEST(test_acquire_next_returns_due_pending_sms_only);
  RUN_TEST(test_mark_sending_and_sent_removes_item_from_queue);
  RUN_TEST(test_mark_sent_frees_capacity_for_new_sms);
  RUN_TEST(test_mark_failed_sets_retry_metadata_and_truncates_error);
  return UNITY_END();
}
