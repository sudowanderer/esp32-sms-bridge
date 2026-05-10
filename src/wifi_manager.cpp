#include "wifi_manager.h"

#include "config_store.h"
#include "logger.h"

#include <WiFi.h>
#include <stdio.h>

static WifiManagerCore wifiCore;

static bool hasWifiCredentials() {
  return ConfigStoreCore::hasWifiConfig(configStoreGet());
}

static void startWifiConnection() {
  const DeviceConfig& config = configStoreGet();
  char message[96];
  snprintf(message, sizeof(message), "wifi_connect ssid=%s", config.wifiSsid);
  logInfo(message);

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid, config.wifiPassword);
}

void wifiManagerBegin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  wifiCore.begin(hasWifiCredentials(), millis());
  if (!hasWifiCredentials()) {
    logWarn("wifi_status=unconfigured");
    return;
  }

  if (wifiCore.consumeConnectRequest()) {
    startWifiConnection();
  }
}

void wifiManagerPoll(uint32_t nowMs) {
  const WifiManagerStatus previousStatus = wifiCore.status();
  wifiCore.poll(WiFi.status() == WL_CONNECTED, nowMs);

  if (wifiCore.status() != previousStatus) {
    char message[96];
    snprintf(message, sizeof(message), "wifi_status=%s", wifiManagerStatusName());
    logInfo(message);
    if (wifiCore.status() == WifiManagerStatus::Connected) {
      snprintf(message, sizeof(message), "wifi_ip=%s", WiFi.localIP().toString().c_str());
      logInfo(message);
    }
  }

  if (wifiCore.consumeConnectRequest()) {
    startWifiConnection();
  }
}

bool wifiManagerIsConfigured() {
  return hasWifiCredentials();
}

bool wifiManagerIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

WifiManagerStatus wifiManagerStatus() {
  return wifiCore.status();
}

const char* wifiManagerStatusName() {
  switch (wifiCore.status()) {
    case WifiManagerStatus::Unconfigured:
      return "unconfigured";
    case WifiManagerStatus::Disconnected:
      return "disconnected";
    case WifiManagerStatus::Connecting:
      return "connecting";
    case WifiManagerStatus::Connected:
      return "connected";
  }

  return "unknown";
}

IPAddress wifiManagerLocalIp() {
  return WiFi.localIP();
}
