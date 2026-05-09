#include "modem_at.h"

#include "modem_at_core.h"

#include <Arduino.h>

static constexpr uint8_t kModemRxPin = 4;
static constexpr uint8_t kModemTxPin = 3;
static constexpr uint32_t kModemBaud = 115200;

static ModemAtCore modemAtCore;

static void writeSerial1(const char* data, void* userData) {
  (void)userData;
  Serial1.print(data);
}

void modemAtBegin() {
  Serial1.begin(kModemBaud, SERIAL_8N1, kModemRxPin, kModemTxPin);
  modemAtCore.begin(millis());
}

void modemAtPoll() {
  // 只有这里读取 Serial1。其他模块只能通过 modemAtSubmit() 排队发送 AT 命令。
  while (Serial1.available() > 0) {
    modemAtCore.onByte(static_cast<char>(Serial1.read()));
  }

  modemAtCore.poll(millis(), writeSerial1, nullptr);
}

bool modemAtSubmit(const char* command, uint32_t timeoutMs, ModemAtCallback callback, void* userData) {
  return modemAtCore.submit(command, timeoutMs, callback, userData);
}

void modemAtSetUrcCallback(ModemUrcCallback callback, void* userData) {
  modemAtCore.setUrcCallback(callback, userData);
}

bool modemAtIsBusy() {
  return modemAtCore.isBusy();
}

uint8_t modemAtQueueDepth() {
  return modemAtCore.queueDepth();
}
