#include "Bridge.h"
#include "FTMSServer.h"
#include "iFitClient.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Display.h"

// Global State
BridgeState bridgeState;

// Modules
FTMSServer ftmsServer;
iFitClient ifitClient;

void setup() {
  Serial.begin(115200);

  // Wait for USB Serial to Connect (for debugging)
  // Give it 5 seconds max, else continue
  unsigned long start = millis();
  while (!Serial && millis() - start < 5000) {
    delay(10);
  }
  delay(1000);

  Serial.println("--- Starting ESP32 Treadmill Bridge ---");
#ifdef BUILD_TIMESTAMP
  Serial.println("--- Firmware Built: " BUILD_TIMESTAMP " ---");
#else
  Serial.println("--- Firmware Built: " __DATE__ " " __TIME__ " ---");
#endif
  Serial.flush(); // Ensure safe printing

  // Initialize display first
  Serial.println("Init Display...");
  Serial.flush(); // Ensure we see this before any crash/hang
  display.init();
  Serial.println("Display Init Done.");

  // Initialize NimBLE
  Serial.println("Init BLE...");
  NimBLEDevice::init(BRIDGE_SERVER_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max Power
  Serial.println("BLE Init Done.");

  // Initialize Modules
  Serial.println("Init Server...");
  ftmsServer.init(); // Start Advertising

  Serial.println("Init Client...");
  ifitClient.init(); // Prepare Scanning

  Serial.println("Bridge Initialized.");
}

void loop() {
  // 1. Client Loop (Scan/Connect/Handshake/Poll)
  ifitClient.loop();

  // 2. Server Loop (Update Characteristics)
  ftmsServer.update();

  // 3. Update Display
  static uint32_t last_display_update = 0;
  if (millis() - last_display_update > 250) {
    last_display_update = millis();
    display.update();
  }

  // 3. Periodic Status Print (Heartbeat)
  static uint32_t last_print = 0;
  if (millis() - last_print > 2000) {
    last_print = millis();
    if (bridgeState.connected_to_ifit) {
      uint32_t total_s = bridgeState.elapsed_time_s;
      uint8_t h = total_s / 3600;
      uint8_t m = (total_s % 3600) / 60;
      uint8_t s = total_s % 60;

      Serial.printf("Status: Connected | Spd: %.1f MPH | Inc: %.1f%% | Time: "
                    "%02d:%02d:%02d | Dist: %.2f mi | Cal: %d\n",
                    bridgeState.speed_kph * 0.621371f, // Convert KPH to MPH
                    bridgeState.incline_pct, h, m, s,
                    bridgeState.distance_m /
                        1609.34f, // Convert Meters to Miles
                    bridgeState.calories);
    } else {
      Serial.println("Status: Scanning for iFit Treadmill...");
    }
  }

  // 4. Yield
  delay(10);
}
