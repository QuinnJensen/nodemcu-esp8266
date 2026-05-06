// web_ui.h
#pragma once
#include <Arduino.h>

void startMainWebUi();
void serviceMainWebUi();
void serviceDeferredWebActions();

void handleHomePage();
void handleAppJs();
void handleStyleCss();
void handleApiStatus();
void handleApiTemps();
void handleApiWater();
void handleApiConfig();

void handleApiScanSensors();
void handleApiSampleWater();

void handlePostServicesConfig();
void handlePostWaterConfig();
void handlePostSensorRename();
