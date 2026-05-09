#include "wifi_manager_core.h"

void WifiManagerCore::begin(bool hasCredentials, uint32_t nowMs) {
  hasCredentials_ = hasCredentials;
  connectRequested_ = false;
  lastConnectAttemptMs_ = nowMs;

  if (!hasCredentials_) {
    status_ = WifiManagerStatus::Unconfigured;
    return;
  }

  status_ = WifiManagerStatus::Connecting;
  connectRequested_ = true;
}

void WifiManagerCore::poll(bool stationConnected, uint32_t nowMs) {
  if (!hasCredentials_) {
    status_ = WifiManagerStatus::Unconfigured;
    connectRequested_ = false;
    return;
  }

  if (stationConnected) {
    status_ = WifiManagerStatus::Connected;
    connectRequested_ = false;
    return;
  }

  if (status_ == WifiManagerStatus::Connected) {
    status_ = WifiManagerStatus::Disconnected;
    lastConnectAttemptMs_ = nowMs;
    return;
  }

  if (status_ == WifiManagerStatus::Unconfigured) {
    status_ = WifiManagerStatus::Disconnected;
  }

  if (nowMs - lastConnectAttemptMs_ >= kReconnectIntervalMs) {
    status_ = WifiManagerStatus::Connecting;
    lastConnectAttemptMs_ = nowMs;
    connectRequested_ = true;
  }
}

bool WifiManagerCore::consumeConnectRequest() {
  if (!connectRequested_) {
    return false;
  }

  connectRequested_ = false;
  return true;
}

WifiManagerStatus WifiManagerCore::status() const {
  return status_;
}

uint32_t WifiManagerCore::lastConnectAttemptMs() const {
  return lastConnectAttemptMs_;
}
