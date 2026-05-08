// wifi_portal.h
#pragma once
#include <Arduino.h>

// Optional display callbacks — register before runStartupPortalIfNeeded()
// so wifi_portal doesn't depend on sketch-local display_ui.h
struct WifiPortalDisplay {
  void (*showPortal)()                          = nullptr;
  void (*showCountdown)(uint8_t secondsLeft)    = nullptr;
  void (*setStatus)(const String& msg,
                    unsigned long holdMs)        = nullptr;
};

void setWifiPortalDisplayCallbacks(const WifiPortalDisplay& cb);

void runStartupPortalIfNeeded(const char* ssidSuffix);
void serviceWifiPortal();
bool forcePortalRequested();
bool startupReconfigRequested();
void startPortalAndConnect(bool forcePortal, const char* ssidSuffix);
void saveConfigCallback();
