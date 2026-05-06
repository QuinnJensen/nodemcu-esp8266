// display_ui.h
#pragma once
#include <Arduino.h>

void initDisplayUi();
void updateDisplayUi();
void renderDisplay();
void showPortalScreen();
void showStartupReconfigCountdown(uint8_t secondsLeft);
void setStatusMessage(const String& msg, unsigned long holdMs = 3000);
void drawSpinner(int cx, int cy, uint8_t frame);
void setBlueLed(bool on);
void flashBlueLed(unsigned int onMs = 25);
String formatHeaderLine(const String& deviceId, const String& ssid);
void kickActivitySpinner(unsigned long durationMs = 1500);
