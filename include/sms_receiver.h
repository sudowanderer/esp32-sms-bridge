#pragma once

#include <stdint.h>

struct SmsMessage {
  char sender[32];
  char timestamp[32];
  char text[1536];
  char pdu[384];
  bool isConcat;
  bool concatComplete;
  bool concatPartial;
  uint8_t concatRef;
  uint8_t concatPart;
  uint8_t concatTotal;
};

struct SmsStorageNotification {
  char storage[8];
  uint16_t index;
};

using SmsReceivedCallback = void (*)(const SmsMessage& message, void* userData);
using SmsErrorCallback = void (*)(const char* reason, const char* rawLine, void* userData);
using SmsStorageCallback = void (*)(const SmsStorageNotification& notification, void* userData);

void smsReceiverBegin();

// 返回 true 表示这一行属于短信接收流程；false 表示调用方可以按普通 URC 处理。
bool smsReceiverOnUrc(const char* line);

// Decode a PDU that was read from storage via AT+CMGR.
bool smsReceiverProcessStoredPdu(const char* pdu);

void smsReceiverSetCallback(SmsReceivedCallback callback, void* userData);
void smsReceiverSetErrorCallback(SmsErrorCallback callback, void* userData);
void smsReceiverSetStorageCallback(SmsStorageCallback callback, void* userData);

// 推进 PDU 等待和长短信缺片超时恢复。
void smsReceiverPoll(uint32_t nowMs);
