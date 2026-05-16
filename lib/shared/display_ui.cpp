#include "display_ui.h"
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "app_state.h"
#include "app_config.h"
#include "util.h"
#include "pins_and_constants.h"

static unsigned long centerDotUntilMs = 0;
static DisplayBodyRenderer sBody = nullptr;
static const char* sPortalAp = "Setup";

static String formatHeaderLine(const String& deviceId, const String& ssid) {
  String header = deviceId;
  if (ssid.length()) header += " (" + ssid + ")";
  const uint8_t maxChars = 20;
  if (header.length() > maxChars) header = header.substring(0, 17) + "...";
  return header;
}

void setDisplayBodyRenderer(DisplayBodyRenderer r) { sBody = r; }
void setDisplayPortalAp(const char* ap) { if (ap && ap[0]) sPortalAp = ap; }

void showOtaProgress(const char* label, unsigned int progress, unsigned int total) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);  display.print("OTA Update");
  display.setCursor(0, 14); display.print(label ? label : "Progress");

  // Progress bar
  display.drawRect(0, 30, 128, 12, SSD1306_WHITE);
  if (total > 0) {
    int barWidth = (progress * 124) / total;
    if (barWidth > 124) barWidth = 124;
    display.fillRect(2, 32, barWidth, 8, SSD1306_WHITE);
    
    // Percentage
    int pct = (progress * 100) / total;
    display.setCursor(0, 48); display.print(pct); display.print("%");
    display.print(" ("); display.print(progress/1024); 
    display.print("/"); display.print(total/1024); display.print(" KB)");
  }
  display.display();
}

void setBlueLed(bool on) {
  digitalWrite(blueLedPin, on ? LOW : HIGH);
}

void flashBlueLed(unsigned int onMs) {
  if (!config.ledEnabled) return;
  setBlueLed(true);
  delay(onMs);
  setBlueLed(false);
}

void pulseSpinnerDot(unsigned long durationMs) {
  centerDotUntilMs = millis() + durationMs;
}

static void drawSpinner(int cx, int cy, uint8_t frame) {
  const int dx[8] = { 0,  4, 6,  4,  0, -4, -6, -4};
  const int dy[8] = {-6, -4, 0,  4,  6,  4,  0, -4};
  for (int i = 0; i < 8; i++) {
    int idx = (i + frame) & 0x07;
    int px = cx + dx[idx];
    int py = cy + dy[idx];
    if (i >= 6)      display.fillCircle(px, py, 2, SSD1306_WHITE);
    else if (i >= 3) display.drawCircle(px, py, 1, SSD1306_WHITE);
    else             display.drawPixel(px, py, SSD1306_WHITE);
  }
  if (millis() < centerDotUntilMs)
    display.fillCircle(cx, cy, 2, SSD1306_WHITE);
}

void showPortalScreen(const char* ssid) {
  if (ssid && ssid[0]) sPortalAp = ssid;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);  display.print("Setup Portal");
  display.setCursor(0, 16); display.print("AP "); display.print(sPortalAp);
  display.setCursor(0, 28); display.print("Go to 192.168.4.1");
  display.setCursor(0, 50); display.print("portal active");
  display.display();
}

void renderDisplay() {
  static uint32_t renderCount = 0;
  if (++renderCount % 10 == 0) Serial.println("[UI] renderDisplay");
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print(formatHeaderLine(safeDeviceId(), WiFi.isConnected() ? WiFi.SSID() : "wifi down"));
  drawSpinner(121, 7, spinnerFrame);

  display.setCursor(0, 8);
  display.print("MQTT ");
  display.print(mqtt.connected() ? "up" : "down");
  display.setCursor(64, 8);
  display.print(lastRssi);
  display.print("dBm");

  if (sBody) sBody();

  // Bottom line — IP address or status message
  display.drawFastHLine(0, 55, 128, SSD1306_WHITE);
  display.setCursor(0, 57);
  if (millis() < statusMsgUntilMs && lastStatusMsg.length()) {
    String msg = lastStatusMsg;
    if (msg.length() > 21) msg = msg.substring(0, 21);
    display.print(msg);
  } else if (WiFi.isConnected()) {
    display.print(WiFi.localIP().toString());
  } else {
    display.print("no ip");
  }

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
  pulseSpinnerDot(durationMs);
}

void showStartupReconfigCountdown(uint8_t secondsLeft) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);  display.print("Starting up...");
  display.setCursor(0, 16); display.print("press FLASH to");
  display.setCursor(0, 28); display.print("reconfig");
  display.setCursor(0, 46); display.print("portal in ");
  display.print(secondsLeft); display.print(" sec");
  display.display();
}

void initDisplayUi() {
  Wire.begin(i2csda, i2cscl);
  Wire.setClock(100000); // 100kHz standard
  if (!display.begin(SSD1306_SWITCHCAPVCC, oledaddr)) {
    Serial.println("OLED init failed");
    for (;;) delay(1000);
  }
  display.clearDisplay();
  display.display();
  pinMode(blueLedPin, OUTPUT);
  setBlueLed(false);
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
