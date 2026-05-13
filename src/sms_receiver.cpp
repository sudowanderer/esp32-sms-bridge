#include "sms_receiver.h"

#include "sms_receiver_core.h"

#include <Arduino.h>
#include <pdulib.h>
#include <string.h>

static SmsReceiverCore smsReceiverCore;
static PDU smsPdu(4096);

static void copyText(char* dest, size_t destCapacity, const char* source) {
  if (dest == nullptr || destCapacity == 0) {
    return;
  }

  if (source == nullptr) {
    dest[0] = '\0';
    return;
  }

  strncpy(dest, source, destCapacity - 1);
  dest[destCapacity - 1] = '\0';
}

static bool decodePduWithLibrary(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  (void)userData;

  if (rawPdu == nullptr || message == nullptr) {
    copyText(error, errorCapacity, "pdu_decode_bad_args");
    return false;
  }

  if (!smsPdu.decodePDU(rawPdu)) {
    copyText(error, errorCapacity, "pdu_decode_failed");
    return false;
  }

  copyText(message->sender, sizeof(message->sender), smsPdu.getSender());
  copyText(message->timestamp, sizeof(message->timestamp), smsPdu.getTimeStamp());
  copyText(message->text, sizeof(message->text), smsPdu.getText());

  int* concatInfo = smsPdu.getConcatInfo();
  if (concatInfo != nullptr && concatInfo[2] > 1 && concatInfo[1] > 0) {
    message->isConcat = true;
    message->concatRef = static_cast<uint8_t>(concatInfo[0]);
    message->concatPart = static_cast<uint8_t>(concatInfo[1]);
    message->concatTotal = static_cast<uint8_t>(concatInfo[2]);
  }

  return true;
}

void smsReceiverBegin() {
  smsReceiverCore.begin(decodePduWithLibrary, nullptr);
}

bool smsReceiverOnUrc(const char* line) {
  return smsReceiverCore.onUrc(line, millis());
}

bool smsReceiverProcessStoredPdu(const char* pdu) {
  return smsReceiverCore.processPdu(pdu, millis());
}

void smsReceiverSetCallback(SmsReceivedCallback callback, void* userData) {
  smsReceiverCore.setReceivedCallback(callback, userData);
}

void smsReceiverSetErrorCallback(SmsErrorCallback callback, void* userData) {
  smsReceiverCore.setErrorCallback(callback, userData);
}

void smsReceiverSetStorageCallback(SmsStorageCallback callback, void* userData) {
  smsReceiverCore.setStorageCallback(callback, userData);
}

void smsReceiverPoll(uint32_t nowMs) {
  smsReceiverCore.poll(nowMs);
}
