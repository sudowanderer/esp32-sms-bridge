#pragma once

#include <stdint.h>

enum class WifiManagerStatus {
  Unconfigured,
  Disconnected,
  Connecting,
  Connected,
};

class WifiManagerCore {
 public:
  static constexpr uint32_t kReconnectIntervalMs = 10000;

  void begin(bool hasCredentials, uint32_t nowMs);
  void poll(bool stationConnected, uint32_t nowMs);

  bool consumeConnectRequest();

  WifiManagerStatus status() const;
  uint32_t lastConnectAttemptMs() const;

 private:
  bool hasCredentials_ = false;
  bool connectRequested_ = false;
  WifiManagerStatus status_ = WifiManagerStatus::Unconfigured;
  uint32_t lastConnectAttemptMs_ = 0;
};
