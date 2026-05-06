#include "display_ui.h"
#include <ESP8266WiFi.h>
#include <math.h>
#include "app_state.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "util.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "pins_and_constants.h"

String formatHeaderLine(const String& deviceId, const String& ssid) {
  String header = deviceId;
  if (ssid.length()) header += " (" + ssid + ")";
  const uint8_t maxChars = 20;
  if (header.length() > maxChars) header = header.substring(0, 17) + "...";
  return header;
}

void setBlueLed(bool on) {
  digitalWrite(blueLedPin, on ? LOW : HIGH);
}

void flashBlueLed(unsigned int onMs) {
  setBlueLed(true);
  delay(onMs);
  setBlueLed(false);
}

void drawSpinner(int cx, int cy, uint8_t frame) {
  const int dx[8] = {0, 4, 6, 4, 0, -4, -6, -4};
  const int dy[8] = {-6, -4, 0, 4, 6, 4, 0, -4};
  for (int i = 0; i < 8; i++) {
    int idx = (i + frame) & 0x07;
    int px = cx + dx[idx];
    int py = cy + dy[idx];
    if (i >= 6) display.fillCircle(px, py, 2, SSD1306_WHITE);
    else if (i >= 3) display.drawCircle(px, py, 1, SSD1306_WHITE);
    else display.drawPixel(px, py, SSD1306_WHITE);
  }
}

void showPortalScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Setup Portal");
  display.setCursor(0, 16);
  display.print("AP TempSensorSetup");
  display.setCursor(0, 28);
  display.print("Go to 192.168.4.1");
  display.setCursor(0, 50);
  display.print("portal active");
  display.display();
}

void renderDisplay() {
  display.clearDisplay();  // clear first

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Header
  display.setCursor(0, 0);
  display.print(formatHeaderLine(safeDeviceId(), WiFi.isConnected() ? WiFi.SSID() : "wifi down"));
  drawSpinner(121, 7, spinnerFrame);

  // MQTT line
  display.setCursor(0, 8);
  display.print("MQTT ");
  display.print(mqtt.connected() ? "up" : "down");
  display.setCursor(64, 8);
  display.print(lastRssi);
  display.print("dBm");

  // Sensors
  display.setCursor(0, 16);
  display.print("Sensors(");
  display.print(sensorCount);
  display.print(")");
  if (!useFakeSensors && sensorNetworkDetected && sensorCount == 0) display.print(" offline");
  else if (useFakeSensors) display.print(" sim");

  for (uint8_t row = 0; row < 2; row++) {
    uint8_t y = 28 + (row * 10);
    display.setCursor(0, y);

    if (sensorCount == 0) {
      if (row == 0) display.print("no sensors found");
      continue;
    }

    uint8_t idx = (displayStartSensor + row) % sensorCount;
    String label = sensorNames[idx][0] ? String(sensorNames[idx]) : String("S") + String(idx + 1);
    if (label.length() > 7) label = label.substring(0, 7);
    while (label.length() < 7) label += " ";

    String addr = sensorAddressString(idx).substring(10);
    display.print(addr);
    display.print(" ");
    display.print(label);
    display.print(" ");
    if (isnan(sensorTempsC[idx])) display.print("disc");
    else {
      float tf = sensorTempsC[idx] * 9.0f / 5.0f + 32.0f;
      display.print(String(tf, 1));
      display.print("F");
    }
  }

  // Water line + heartbeat counter
  display.setCursor(0, 54);
  display.print("Water ");
  display.print(waterLevelLabel(waterLevelIndex));
  
  display.display();
}

void setStatusMessage(const String& msg, unsigned long holdMs) {
  lastStatusMsg = msg;
  statusMsgUntilMs = millis() + holdMs;
  Serial.println(msg);
}

void kickActivitySpinner(unsigned long durationMs) {
  mqttTrafficActive = true;
  lastTrafficAnimMs = millis() + durationMs - 1500UL;
  spinnerFrame = (spinnerFrame + 1) & 0x07;
}

void showStartupReconfigCountdown(uint8_t secondsLeft) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Starting up...");
  display.setCursor(0, 16);
  display.print("press FLASH to");
  display.setCursor(0, 28);
  display.print("reconfig");
  display.setCursor(0, 46);
  display.print("portal in ");
  display.print(secondsLeft);
  display.print(" sec");
  display.display();
}

void initDisplayUi() {
  Wire.begin(i2csda, i2cscl);
  if (!display.begin(SSD1306_SWITCHCAPVCC, oledaddr)) {
    Serial.println("OLED init failed");
    for (;;) delay(1000);
  }
  display.clearDisplay();
  display.display();
}

void updateDisplayUi() {
  unsigned long now = millis();

  static unsigned long lastSpin = 0;
  if (now - lastSpin >= 150) {
    spinnerFrame = (spinnerFrame + 1) & 0x07;
    lastSpin = now;
  }

  if (now - lastDisplayMs >= displayintervalms) {
    renderDisplay();
    lastDisplayMs = now;
  }
}
