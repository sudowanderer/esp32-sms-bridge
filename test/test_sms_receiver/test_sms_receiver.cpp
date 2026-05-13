#include <unity.h>

#include "sms_receiver_core.h"

#include <string.h>

struct TestContext {
  SmsMessage lastMessage;
  SmsMessage lastDecodedMessage;
  SmsStorageNotification lastStorage;
  int receivedCount;
  int decodedCount;
  int storageCount;
  char lastError[64];
  char lastRawLine[384];
  int errorCount;
  bool decoderShouldPass;
  bool decoderCalled;
};

static void resetContext(TestContext& ctx) {
  memset(&ctx, 0, sizeof(ctx));
  ctx.decoderShouldPass = true;
}

static void copyText(char* dest, size_t destCapacity, const char* source) {
  strncpy(dest, source, destCapacity - 1);
  dest[destCapacity - 1] = '\0';
}

static const char* kRealLongPdu1 =
    "0891683110801105F0640DA1688121815695F20008625001312411238C0500038A040153174EAC5E02516C5B895C4063D0919260A8FF1A6839636E53174EAC5E024EBA6C11653F5E9C901A544AFF0C81EA00320030003200355E74003867080032003965E596F665F68D7781F300320030003200355E7400396708003365E500320034002065F66B62FF0C53174EAC5E02516857DF4E3A51C07A7A96505236533AFF0C79816B625347";
static const char* kRealLongPdu2 =
    "0891683110801105F0640DA1688121815695F20008625001312411238C0500038A0402653E672A7ECF627951C6768465E04EBA673A30017A7F8D8A673A4EE553CA98CE7B5D30016C14740330015B54660E706F7B495F7154CD98DE884C5B895168768472694F53FF0C90E85206533A52066BB596505236653E98DE9E1F7C7B30028BE660C58BF767E58BE2300A53174EAC5E024EBA6C11653F5E9C51734E8E5728672C5E0290E85206";
static const char* kRealLongPdu3 =
    "0891683110801105F0640DA1688121815695F20008625001312411238C0500038A0403884C653F533A57DF518579816B62653E98DE30015347653E5F7154CD98DE884C5B89516876849E1F7C7B52A87269548C51764ED672694F537684901A544A300B300A53174EAC5E024EBA6C11653F5E9C51734E8E589E8BBE51C07A7A96505236533A7684901A544A300B30026CFC73348FC7654F660E4F6054E64F60660E660E6709548C4ED6";
static const char* kRealLongPdu4 =
    "0891683110801105F0640DA1688121815695F2000862500131241123600500038A04044EEC670980547CFB54178FD84E0D662F89817ED94ED64EEC94B14F608FD8597DD83DDE0530018FD951E05929768459296C1490FD5F8851B75F888212670D768459296C14771F4E0D597D73A9621160F34F6076845FC34E5F4E0D";

static bool fakeDecodePdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  if (!ctx->decoderShouldPass) {
    copyText(error, errorCapacity, "fake_decode_failed");
    return false;
  }

  copyText(message->sender, sizeof(message->sender), "+8613800138000");
  copyText(message->timestamp, sizeof(message->timestamp), "260509120000");
  copyText(message->text, sizeof(message->text), "hello");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  return true;
}

static bool fakeDecodeConcatPdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  (void)error;
  (void)errorCapacity;
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  copyText(message->sender, sizeof(message->sender), "+8613800138000");
  copyText(message->timestamp, sizeof(message->timestamp), "260509120001");
  copyText(message->text, sizeof(message->text), "part one");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  message->isConcat = true;
  message->concatRef = 42;
  message->concatPart = 1;
  message->concatTotal = 2;
  return true;
}

static bool fakeDecodeConcatByPdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  (void)error;
  (void)errorCapacity;
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  copyText(message->sender, sizeof(message->sender), "+8613800138000");
  copyText(message->timestamp, sizeof(message->timestamp), "260509120001");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  message->isConcat = true;
  message->concatRef = 42;
  message->concatTotal = 3;

  if (strcmp(rawPdu, "AA01") == 0) {
    message->concatPart = 1;
    copyText(message->text, sizeof(message->text), "part one ");
  } else if (strcmp(rawPdu, "AA02") == 0) {
    message->concatPart = 2;
    copyText(message->text, sizeof(message->text), "part two ");
  } else if (strcmp(rawPdu, "AA03") == 0) {
    message->concatPart = 3;
    copyText(message->text, sizeof(message->text), "part three");
  } else if (strcmp(rawPdu, "BB01") == 0) {
    message->concatRef = 43;
    message->concatPart = 1;
    message->concatTotal = 2;
    copyText(message->text, sizeof(message->text), "other one ");
  } else if (strcmp(rawPdu, "BB02") == 0) {
    message->concatRef = 43;
    message->concatPart = 2;
    message->concatTotal = 2;
    copyText(message->text, sizeof(message->text), "other two");
  } else if (strcmp(rawPdu, "CC00") == 0) {
    message->concatPart = 0;
    copyText(message->text, sizeof(message->text), "invalid zero");
  } else if (strcmp(rawPdu, "CC04") == 0) {
    message->concatPart = 4;
    copyText(message->text, sizeof(message->text), "invalid high");
  } else if (strcmp(rawPdu, "CC11") == 0) {
    message->concatPart = 1;
    message->concatTotal = 11;
    copyText(message->text, sizeof(message->text), "too many");
  } else {
    message->concatPart = 1;
    copyText(message->text, sizeof(message->text), "unknown");
  }

  return true;
}

static bool fakeDecodeConcatCachePdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  (void)error;
  (void)errorCapacity;
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  copyText(message->sender, sizeof(message->sender), "+8613800138000");
  copyText(message->timestamp, sizeof(message->timestamp), "260509120001");
  copyText(message->text, sizeof(message->text), "cached");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  message->isConcat = true;
  message->concatPart = 1;
  message->concatTotal = 2;
  message->concatRef = static_cast<uint8_t>(rawPdu[1]);
  return true;
}

static bool fakeDecodeRealLongPdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  (void)error;
  (void)errorCapacity;
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  copyText(message->sender, sizeof(message->sender), "8618121865592");
  copyText(message->timestamp, sizeof(message->timestamp), "26051013421132");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  message->isConcat = true;
  message->concatRef = 138;
  message->concatTotal = 4;

  if (strcmp(rawPdu, kRealLongPdu1) == 0) {
    message->concatPart = 1;
    copyText(message->text, sizeof(message->text),
             "北京市公安局提醒您：根据北京市人民政府通告，自2025年8月29日零时起至2025年9月3日24 时止，北京市全域为净空限制区，禁止升");
  } else if (strcmp(rawPdu, kRealLongPdu2) == 0) {
    message->concatPart = 2;
    copyText(message->text, sizeof(message->text),
             "放未经批准的无人机、穿越机以及风筝、气球、孔明灯等影响飞行安全的物体，部分区分段限制放飞鸟类。详情请查询《北京市人民政府关于在本市部分");
  } else if (strcmp(rawPdu, kRealLongPdu3) == 0) {
    message->concatPart = 3;
    copyText(message->text, sizeof(message->text),
             "行政区域内禁止放飞、升放影响飞行安全的鸟类动物和其他物体的通告》《北京市人民政府关于增设净空限制区的通告》。泼猴过敏明你哦你明明有和他");
  } else {
    message->concatPart = 4;
    copyText(message->text, sizeof(message->text),
             "们有联系吗还不是要给他们钱你还好😅、这几天的天气都很冷很舒服的天气真不好玩我想你的心也不");
  }

  return true;
}

static void captureSms(const SmsMessage& message, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->lastMessage = message;
  ctx->receivedCount++;
}

static void captureDecodedSms(const SmsMessage& message, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->lastDecodedMessage = message;
  ctx->decodedCount++;
}

static void captureError(const char* reason, const char* rawLine, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  copyText(ctx->lastError, sizeof(ctx->lastError), reason);
  copyText(ctx->lastRawLine, sizeof(ctx->lastRawLine), rawLine);
  ctx->errorCount++;
}

static void captureStorage(const SmsStorageNotification& notification, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->lastStorage = notification;
  ctx->storageCount++;
}

void test_cmt_plus_pdu_emits_sms_message() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("0891683108200505F0", 101));

  TEST_ASSERT_TRUE(ctx.decoderCalled);
  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_EQUAL_STRING("+8613800138000", ctx.lastMessage.sender);
  TEST_ASSERT_EQUAL_STRING("260509120000", ctx.lastMessage.timestamp);
  TEST_ASSERT_EQUAL_STRING("hello", ctx.lastMessage.text);
  TEST_ASSERT_EQUAL_STRING("0891683108200505F0", ctx.lastMessage.pdu);
  TEST_ASSERT_FALSE(ctx.lastMessage.isConcat);
  TEST_ASSERT_EQUAL(0, ctx.errorCount);
}

void test_non_sms_urc_is_not_consumed() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);

  TEST_ASSERT_FALSE(core.onUrc("+CEREG: 0,1", 100));
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_cmti_urc_emits_storage_notification() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setStorageCallback(captureStorage, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMTI: \"SM\",37", 100));

  TEST_ASSERT_EQUAL(1, ctx.storageCount);
  TEST_ASSERT_EQUAL_STRING("SM", ctx.lastStorage.storage);
  TEST_ASSERT_EQUAL(37, ctx.lastStorage.index);
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_process_stored_pdu_decodes_without_cmt_header() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.processPdu("0891683108200505F0", 100));

  TEST_ASSERT_TRUE(ctx.decoderCalled);
  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_EQUAL_STRING("0891683108200505F0", ctx.lastMessage.pdu);
  TEST_ASSERT_EQUAL(0, ctx.errorCount);
}

void test_stored_concat_part_emits_decoded_callback_before_merge() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setDecodedCallback(captureDecodedSms, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.processPdu("AA01", 100));

  TEST_ASSERT_EQUAL(1, ctx.decodedCount);
  TEST_ASSERT_EQUAL(0, ctx.receivedCount);
  TEST_ASSERT_TRUE(ctx.lastDecodedMessage.isConcat);
  TEST_ASSERT_EQUAL(42, ctx.lastDecodedMessage.concatRef);
  TEST_ASSERT_EQUAL(1, ctx.lastDecodedMessage.concatPart);
  TEST_ASSERT_EQUAL(3, ctx.lastDecodedMessage.concatTotal);
}

void test_non_hex_pdu_reports_error() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("not-a-pdu", 101));

  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("pdu_not_hex", ctx.lastError);
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_odd_length_pdu_reports_error() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("ABC", 101));

  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("pdu_not_hex", ctx.lastError);
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_decode_failure_reports_error() {
  TestContext ctx;
  resetContext(ctx);
  ctx.decoderShouldPass = false;
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("0891683108200505F0", 101));

  TEST_ASSERT_TRUE(ctx.decoderCalled);
  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("fake_decode_failed", ctx.lastError);
}

void test_concat_sms_waits_until_all_parts_arrive() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("AA01", 101));
  TEST_ASSERT_EQUAL(0, ctx.receivedCount);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("AA02", 201));
  TEST_ASSERT_EQUAL(0, ctx.receivedCount);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 300));
  TEST_ASSERT_TRUE(core.onUrc("AA03", 301));
  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_TRUE(ctx.lastMessage.isConcat);
  TEST_ASSERT_EQUAL(42, ctx.lastMessage.concatRef);
  TEST_ASSERT_EQUAL(3, ctx.lastMessage.concatPart);
  TEST_ASSERT_EQUAL(3, ctx.lastMessage.concatTotal);
  TEST_ASSERT_TRUE(ctx.lastMessage.concatComplete);
  TEST_ASSERT_FALSE(ctx.lastMessage.concatPartial);
  TEST_ASSERT_EQUAL_STRING("part one part two part three", ctx.lastMessage.text);
  TEST_ASSERT_EQUAL_STRING("AA01", ctx.lastMessage.pdu);
}

void test_concat_sms_merges_out_of_order_parts() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("AA03", 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("AA01", 201));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 300));
  TEST_ASSERT_TRUE(core.onUrc("AA02", 301));

  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_EQUAL_STRING("part one part two part three", ctx.lastMessage.text);
}

void test_concat_sms_ignores_duplicate_parts() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("AA01", 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("AA01", 201));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 300));
  TEST_ASSERT_TRUE(core.onUrc("AA02", 301));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 400));
  TEST_ASSERT_TRUE(core.onUrc("AA03", 401));

  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_EQUAL_STRING("part one part two part three", ctx.lastMessage.text);
}

void test_concat_sms_keeps_different_refs_separate() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("AA01", 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("BB01", 201));
  TEST_ASSERT_EQUAL(0, ctx.receivedCount);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 300));
  TEST_ASSERT_TRUE(core.onUrc("BB02", 301));
  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_EQUAL(43, ctx.lastMessage.concatRef);
  TEST_ASSERT_EQUAL_STRING("other one other two", ctx.lastMessage.text);
}

void test_concat_sms_timeout_emits_partial_with_missing_marker() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("AA01", 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("AA03", 201));
  TEST_ASSERT_EQUAL(0, ctx.receivedCount);

  core.poll(30101);

  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_TRUE(ctx.lastMessage.isConcat);
  TEST_ASSERT_FALSE(ctx.lastMessage.concatComplete);
  TEST_ASSERT_TRUE(ctx.lastMessage.concatPartial);
  TEST_ASSERT_EQUAL_STRING("part one [缺失分段2]part three", ctx.lastMessage.text);
}

void test_concat_sms_rejects_invalid_part_metadata() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatByPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("CC00", 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("CC04", 201));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 300));
  TEST_ASSERT_TRUE(core.onUrc("CC11", 301));

  TEST_ASSERT_EQUAL(0, ctx.receivedCount);
  TEST_ASSERT_EQUAL(3, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("concat_invalid_part", ctx.lastError);
}

void test_concat_sms_cache_full_does_not_overwrite_existing_groups() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatCachePdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("CA01", 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 200));
  TEST_ASSERT_TRUE(core.onUrc("CB01", 201));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 300));
  TEST_ASSERT_TRUE(core.onUrc("CC01", 301));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 400));
  TEST_ASSERT_TRUE(core.onUrc("CD01", 401));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 500));
  TEST_ASSERT_TRUE(core.onUrc("CE01", 501));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 600));
  TEST_ASSERT_TRUE(core.onUrc("CF01", 601));

  TEST_ASSERT_EQUAL(0, ctx.receivedCount);
  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("concat_cache_full", ctx.lastError);
}

void test_concat_sms_merges_real_four_part_pdu_fixture_once() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeRealLongPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,152", 100));
  TEST_ASSERT_TRUE(core.onUrc(kRealLongPdu1, 101));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,152", 200));
  TEST_ASSERT_TRUE(core.onUrc(kRealLongPdu2, 201));
  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,152", 300));
  TEST_ASSERT_TRUE(core.onUrc(kRealLongPdu3, 301));
  TEST_ASSERT_EQUAL(0, ctx.receivedCount);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,152", 400));
  TEST_ASSERT_TRUE(core.onUrc(kRealLongPdu4, 401));

  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_TRUE(ctx.lastMessage.concatComplete);
  TEST_ASSERT_FALSE(ctx.lastMessage.concatPartial);
  TEST_ASSERT_EQUAL(138, ctx.lastMessage.concatRef);
  TEST_ASSERT_EQUAL(4, ctx.lastMessage.concatTotal);
  TEST_ASSERT_TRUE(strstr(ctx.lastMessage.text, "北京市公安局提醒您") != nullptr);
  TEST_ASSERT_TRUE(strstr(ctx.lastMessage.text, "我想你的心也不") != nullptr);
}

void test_waiting_for_pdu_times_out() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  core.poll(5100);

  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("pdu_timeout", ctx.lastError);
  TEST_ASSERT_EQUAL_STRING("+CMT: ,24", ctx.lastRawLine);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_cmt_plus_pdu_emits_sms_message);
  RUN_TEST(test_non_sms_urc_is_not_consumed);
  RUN_TEST(test_cmti_urc_emits_storage_notification);
  RUN_TEST(test_process_stored_pdu_decodes_without_cmt_header);
  RUN_TEST(test_stored_concat_part_emits_decoded_callback_before_merge);
  RUN_TEST(test_non_hex_pdu_reports_error);
  RUN_TEST(test_odd_length_pdu_reports_error);
  RUN_TEST(test_decode_failure_reports_error);
  RUN_TEST(test_concat_sms_waits_until_all_parts_arrive);
  RUN_TEST(test_concat_sms_merges_out_of_order_parts);
  RUN_TEST(test_concat_sms_ignores_duplicate_parts);
  RUN_TEST(test_concat_sms_keeps_different_refs_separate);
  RUN_TEST(test_concat_sms_timeout_emits_partial_with_missing_marker);
  RUN_TEST(test_concat_sms_rejects_invalid_part_metadata);
  RUN_TEST(test_concat_sms_cache_full_does_not_overwrite_existing_groups);
  RUN_TEST(test_concat_sms_merges_real_four_part_pdu_fixture_once);
  RUN_TEST(test_waiting_for_pdu_times_out);
  return UNITY_END();
}
