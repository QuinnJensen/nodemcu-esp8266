#pragma once
#include <Arduino.h>

void initDisplayUi();
void updateDisplayUi();
void renderDisplay();
void setStatusMessage(const String& msg, unsigned long holdMs);
void kickActivitySpinner(unsigned long durationMs);
void pulseSpinnerDot(unsigned long durationMs);  // light center dot for durationMs
void showPortalScreen();
void showStartupReconfigCountdown(uint8_t secondsLeft);
void setBlueLed(bool on);
void flashBlueLed(unsigned int onMs);  // retained for any callers not yet migrated
