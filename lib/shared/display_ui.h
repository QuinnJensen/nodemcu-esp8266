// display_ui.h - shared display helpers
#pragma once
#include <Arduino.h>

// Sketch supplies a function that draws the body of the display
// (everything below the header). This is called inside renderDisplay()
// on a clean buffer; the shared header (deviceId / SSID / spinner /
// MQTT / RSSI) is drawn by the shared base; the bottom status line is
// drawn afterwards. The body region is roughly y=16..52.
typedef void (*DisplayBodyRenderer)();

void initDisplayUi();
void updateDisplayUi();
void renderDisplay();
void setStatusMessage(const String& msg, unsigned long holdMs);
void kickActivitySpinner(unsigned long durationMs);
void pulseSpinnerDot(unsigned long durationMs);
void showPortalScreen(const char* ssid = nullptr);
void showStartupReconfigCountdown(uint8_t secondsLeft);
void showOtaProgress(const char* label, unsigned int progress, unsigned int total);
void setBlueLed(bool on);
void flashBlueLed(unsigned int onMs);
void setDisplayBodyRenderer(DisplayBodyRenderer r);
void setDisplayPortalAp(const char* ap);  // override AP name shown on portal screen
