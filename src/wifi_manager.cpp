#include "wifi_manager.h"

#include <WiFi.h>

#if __has_include("local_wifi_config.h")
#include "local_wifi_config.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

static WifiManagerCore wifiCore;

static bool hasWifiCredentials() {
  return WIFI_SSID[0] != '\0';
}

static void startWifiConnection() {
  Serial.print("wifi_connect ssid=");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void wifiManagerBegin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  wifiCore.begin(hasWifiCredentials(), millis());
  if (!hasWifiCredentials()) {
    Serial.println("wifi_status=unconfigured");
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
    Serial.print("wifi_status=");
    Serial.println(wifiManagerStatusName());
    if (wifiCore.status() == WifiManagerStatus::Connected) {
      Serial.print("wifi_ip=");
      Serial.println(WiFi.localIP());
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
