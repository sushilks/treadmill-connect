#include "FTMSServer.h"

// Connection Callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer) override {
    bridgeState.server_advertising = false;
    bridgeState.connected_to_ftms = true;
    Serial.println("FTMS Client Connected");
  };
  void onDisconnect(NimBLEServer *pServer) override {
    bridgeState.server_advertising = true;
    bridgeState.connected_to_ftms = false;
    Serial.println("FTMS Client Disconnected");
    NimBLEDevice::startAdvertising();
  }
};

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() < 1)
      return;

    uint8_t opcode = value[0];

    // 0x00: Request Control
    if (opcode == 0x00) {
      // Indicate Success (0x80, Opcode, Result(1=Success))
      uint8_t resp[] = {0x80, 0x00, 0x01};
      pCharacteristic->setValue(resp, 3);
      pCharacteristic->indicate();
    }
    // 0x02: Set Target Speed (Uint16 0.01 KPH)
    else if (opcode == 0x02 && value.length() >= 3) {
      uint16_t kph_raw = (uint8_t)value[1] | ((uint8_t)value[2] << 8);
      // Pass raw KPH (0.01 KPH resolution)
      // Treadmill uses KPH internally (confirmed by telemetry), so we send KPH.

      bridgeState.pending_control_type = 1; // Speed
      bridgeState.pending_control_value = kph_raw;
      bridgeState.pending_control = true;
      Serial.printf("FTMS Set Speed: %d (0.01 KPH)\n", kph_raw);

      // Response
      uint8_t resp[] = {0x80, 0x02, 0x01};
      pCharacteristic->setValue(resp, 3);
      pCharacteristic->indicate();
    }
    // 0x03: Set Target Incline (Int16 0.1 %)
    else if (opcode == 0x03 && value.length() >= 3) {
      int16_t inc_raw = (uint8_t)value[1] | ((uint8_t)value[2] << 8);
      // Convert 0.1% -> 0.01% (Multiply by 10)
      int16_t ifit_inc = inc_raw * 10;

      bridgeState.pending_control_type = 2; // Incline
      bridgeState.pending_control_value = ifit_inc;
      bridgeState.pending_control = true;

      // Response
      uint8_t resp[] = {0x80, 0x03, 0x01};
      pCharacteristic->setValue(resp, 3);
      pCharacteristic->indicate();
    }
  }
};

void FTMSServer::init() {
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Service
  NimBLEService *pService = pServer->createService(UUID_FTMS_SERVICE);

  // Data Char (Notify)
  pDataChar =
      pService->createCharacteristic(UUID_FTMS_DATA, NIMBLE_PROPERTY::NOTIFY);

  // Control Point
  pControlChar = pService->createCharacteristic(UUID_FTMS_CONTROL_POINT,
                                                NIMBLE_PROPERTY::WRITE |
                                                    NIMBLE_PROPERTY::INDICATE);
  pControlChar->setCallbacks(new ControlCallbacks());

  // Feature
  NimBLECharacteristic *pFeature =
      pService->createCharacteristic(UUID_FTMS_FEATURE, NIMBLE_PROPERTY::READ);
  // Byte 0: 0x0C (Dist, Inc)
  // Byte 1: 0x20 (Expended Energy). Matches Packet Flag Bit 7.
  // Byte 4: 0x03 (Target Speed, Target Inc).
  uint8_t feat[] = {0x0C, 0x20, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
  pFeature->setValue(feat, 8);

  // Status
  pStatusChar =
      pService->createCharacteristic(UUID_FTMS_STATUS, NIMBLE_PROPERTY::NOTIFY);

  // Supported Speed Range (Mandatory for Control)
  // 0.01 KPH resolution. Min: 0.5 KPH, Max: 22.0 KPH, Step: 0.1 KPH
  NimBLECharacteristic *pSpeedRange = pService->createCharacteristic(
      UUID_FTMS_SPEED_RANGE, NIMBLE_PROPERTY::READ);
  uint16_t min_spd = 50;   // 0.5 KPH
  uint16_t max_spd = 2200; // 22.0 KPH
  uint16_t inc_spd = 10;   // 0.1 KPH
  uint8_t speed_range_val[] = {
      (uint8_t)(min_spd & 0xFF), (uint8_t)(min_spd >> 8),
      (uint8_t)(max_spd & 0xFF), (uint8_t)(max_spd >> 8),
      (uint8_t)(inc_spd & 0xFF), (uint8_t)(inc_spd >> 8)};
  pSpeedRange->setValue(speed_range_val, 6);

  // Supported Inclination Range (Mandatory for Control)
  // 0.1% resolution. Min: 0%, Max: 15%, Step: 0.5%
  NimBLECharacteristic *pIncRange = pService->createCharacteristic(
      UUID_FTMS_INCLINE_RANGE, NIMBLE_PROPERTY::READ);
  int16_t min_inc = 0;   // 0%
  int16_t max_inc = 150; // 15%
  uint16_t inc_inc = 5;  // 0.5%
  uint8_t inc_range_val[] = {
      (uint8_t)(min_inc & 0xFF), (uint8_t)(min_inc >> 8),
      (uint8_t)(max_inc & 0xFF), (uint8_t)(max_inc >> 8),
      (uint8_t)(inc_inc & 0xFF), (uint8_t)(inc_inc >> 8)};
  pIncRange->setValue(inc_range_val, 6);

  pService->start();

  // Advertisement
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(UUID_FTMS_SERVICE);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  bridgeState.server_advertising = true;
}

void FTMSServer::update() {
  // Watchdog for Advertising
  if (pServer->getConnectedCount() == 0) {
    if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
      Serial.println("Restoring FTMS Advertising...");
      NimBLEDevice::startAdvertising();
    }
    return;
  }

  if (millis() - bridgeState.last_ftms_update > 1000) {
    bridgeState.last_ftms_update = millis();

    // Treadmill Data Packet (Little Endian)
    // Flags: 2 bytes
    // Bit 0: More Data (0)
    // Bit 1: Avg Speed (0)
    // Bit 2: Total Distance (1)
    // Bit 3: Inclination (1)
    // Bit 4: Elevation Gain (0)
    // Bit 5: Pace (0)
    // Bit 6: Pacing Algo (0)
    // Bit 7: Expended Energy (1)
    // Bit 8: Heart Rate (0)
    // Bit 9: Metabolic Equ (0)
    // Bit 10: Elapsed Time (1)
    // Bit 11: Remaining Time (0)
    // Bit 12: Force/Belt Power (0)
    // Flags = 0x048C => 0000 0100 1000 1100
    uint16_t flags = 0x048C;

    uint8_t packet[30];
    uint8_t idx = 0;

    // Flags (2 bytes)
    packet[idx++] = (uint8_t)(flags & 0xFF);
    packet[idx++] = (uint8_t)((flags >> 8) & 0xFF);

    // Inst Speed (Km/h * 100) (2 bytes)
    // bridgeState.speed_kph is already true KPH.
    uint16_t spd = (uint16_t)(bridgeState.speed_kph * 100);
    packet[idx++] = spd & 0xFF;
    packet[idx++] = (spd >> 8) & 0xFF;

    // Total Distance (Metres) (3 bytes)
    // Total Distance (Metres) (3 bytes)
    // Distance_M is already in Meters.
    uint32_t dist = bridgeState.distance_m;
    packet[idx++] = dist & 0xFF;
    packet[idx++] = (dist >> 8) & 0xFF;
    packet[idx++] = (dist >> 16) & 0xFF;

    // Inclination (Percent * 10) (2 bytes)
    int16_t inc = (int16_t)(bridgeState.incline_pct * 10);
    packet[idx++] = inc & 0xFF;
    packet[idx++] = (inc >> 8) & 0xFF;

    // Ramp Angle Setting (Percent * 10) (2 bytes) - MANDATORY if Incline flag
    // is set We don't have this, so send 0.0 or same as incline? 0.0 is safer.
    int16_t ramp = 0;
    packet[idx++] = ramp & 0xFF;
    packet[idx++] = (ramp >> 8) & 0xFF;

    // Energy (Total Cals, Cals/Hr, Energy/Min)
    uint16_t total_cals = (uint16_t)bridgeState.calories;
    packet[idx++] = total_cals & 0xFF;
    packet[idx++] = (total_cals >> 8) & 0xFF;

    packet[idx++] = 0x00; // Cals/Hr L
    packet[idx++] = 0x00; // Cals/Hr H
    packet[idx++] = 0x00; // Energy/Min

    // Elapsed Time (Seconds) (2 bytes)
    uint16_t time_s = (uint16_t)bridgeState.elapsed_time_s;
    packet[idx++] = time_s & 0xFF;
    packet[idx++] = (time_s >> 8) & 0xFF;

    pDataChar->setValue(packet, idx);
    pDataChar->notify();
  }
}
