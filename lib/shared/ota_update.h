// ota_update.h - shared Over-The-Air update logic
#pragma once
#include <Arduino.h>

/**
 * Initializes both ArduinoOTA (Network Push) and the Web Update Server.
 * Registers handlers on the provided web server for the /update path.
 */
void initOtaUpdate();

/**
 * Pump OTA network tasks. Call in main loop().
 */
void serviceOtaUpdate();

/**
 * Callback signature for sketches to stop sensitive hardware when OTA starts.
 */
typedef void (*OtaNotifyFn)(void);
void setOtaStartCallback(OtaNotifyFn fn);
