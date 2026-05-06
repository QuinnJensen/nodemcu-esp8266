// wifi_portal.h

#pragma once
#include <Arduino.h>

void runStartupPortalIfNeeded();
void serviceWifiPortal();
bool forcePortalRequested();
bool startupReconfigRequested();
void startPortalAndConnect(bool forcePortal);
void saveConfigCallback();
