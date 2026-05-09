#include <Arduino.h>

static constexpr uint8_t kModemRxPin = 4;
static constexpr uint8_t kModemTxPin = 3;
static constexpr uint8_t kModemEnPin = 5;
static constexpr uint32_t kLogIntervalMs = 1000;

static uint32_t lastLogMs = 0;

static bool sendAtAndWaitOk(const char* label, uint32_t timeoutMs) {
  Serial.print("at_test=");
  Serial.print(label);
  Serial.println(" send AT");

  Serial1.println("AT");

  const uint32_t startMs = millis();
  String response;
  while (millis() - startMs < timeoutMs) {
    while (Serial1.available()) {
      const char c = static_cast<char>(Serial1.read());
      response += c;
      Serial.write(c);

      if (response.indexOf("OK") >= 0) {
        Serial.print("at_test=");
        Serial.print(label);
        Serial.println(" result=OK");
        return true;
      }

      if (response.indexOf("ERROR") >= 0) {
        Serial.print("at_test=");
        Serial.print(label);
        Serial.println(" result=ERROR");
        return false;
      }
    }
  }

  Serial.print("at_test=");
  Serial.print(label);
  Serial.println(" result=TIMEOUT");
  return false;
}

static void runModemEnSmokeTest() {
  Serial1.begin(115200, SERIAL_8N1, kModemRxPin, kModemTxPin);
  Serial1.setTimeout(50);

  Serial.println("modem_en_test=begin");
  Serial.println("modem_en_pin=5");
  Serial.println("modem_uart_rx=4 modem_uart_tx=3 baud=115200");
  Serial.println("observe PWR D5 / NET D1 now; test starts in 3 seconds");
  delay(3000);

  pinMode(kModemEnPin, OUTPUT);

  Serial.println("MODEM_EN HIGH: expected modem enabled");
  digitalWrite(kModemEnPin, HIGH);
  delay(3000);
  sendAtAndWaitOk("before_en_low", 1500);

  Serial.println("MODEM_EN LOW: expected modem disabled or reset");
  digitalWrite(kModemEnPin, LOW);
  delay(1000);
  sendAtAndWaitOk("while_en_low", 1500);

  Serial.println("MODEM_EN HIGH: restore modem enabled");
  digitalWrite(kModemEnPin, HIGH);
  delay(6000);
  sendAtAndWaitOk("after_en_high", 1500);

  Serial.println("modem_en_test=end");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP32 SMS bridge serial-only smoke test");
  Serial.print("chip_model=");
  Serial.println(ESP.getChipModel());
  Serial.print("chip_revision=");
  Serial.println(ESP.getChipRevision());
  Serial.print("cpu_mhz=");
  Serial.println(ESP.getCpuFreqMHz());
  Serial.print("flash_size=");
  Serial.println(ESP.getFlashChipSize());

  runModemEnSmokeTest();
}

void loop() {
  const uint32_t now = millis();

  if (now - lastLogMs >= kLogIntervalMs) {
    lastLogMs = now;

    Serial.print("uptime_ms=");
    Serial.print(now);
    Serial.print(" free_heap=");
    Serial.println(ESP.getFreeHeap());
  }
}
