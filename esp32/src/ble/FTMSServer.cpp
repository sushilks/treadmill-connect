#include "FTMSServer.h"

// Connection Callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer) override {
    Serial.println("Client Connected");
    bridgeState.connected_to_ftms = true;

    // Request stable connection parameters
    if (pServer->getConnectedCount() > 0) {
      // Use the original working method for NimBLE 1.x
      NimBLEConnInfo peer = pServer->getPeerInfo(0);
      pServer->updateConnParams(peer.getConnHandle(), 12, 24, 0, 400);
    }
  }

  void onDisconnect(NimBLEServer *pServer) override {
    Serial.println("Client Disconnected");
    bridgeState.connected_to_ftms = false;
    NimBLEDevice::startAdvertising();
  }
};

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  NimBLECharacteristic *pStatusChar = nullptr;

public:
  void setStatusChar(NimBLECharacteristic *status) { pStatusChar = status; }

  void onWrite(NimBLECharacteristic *pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() < 1)
      return;

    uint8_t opcode = value[0];

    // 0x00: Request Control
    if (opcode == 0x00) {
      Serial.println("FTMS: Request Control Received");

      // ORDER: Notify Status (Started) FIRST, then Indicate Response (Success)
      // Matches Python behavior.
      if (pStatusChar) {
        uint8_t stat[] = {0x04}; // Started
        pStatusChar->setValue(stat, 1);
        pStatusChar->notify();
        Serial.println("FTMS Status: Started (0x04)");
      }

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

      // Notify Status Change FIRST
      if (pStatusChar) {
        uint8_t stat[3];
        stat[0] = 0x05; // Speed Changed
        stat[1] = value[1];
        stat[2] = value[2];
        pStatusChar->setValue(stat, 3);
        pStatusChar->notify();
      }

      // Indicate Success
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
      Serial.printf("FTMS Set Incline: %d (0.1%%) -> iFit Val: %d\n", inc_raw,
                    ifit_inc);

      // Notify Status Change FIRST
      if (pStatusChar) {
        uint8_t stat[3];
        stat[0] = 0x06; // Incline Changed
        stat[1] = value[1];
        stat[2] = value[2];
        pStatusChar->setValue(stat, 3);
        pStatusChar->notify();
      }

      // Indicate Success
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

  // 1. Data Char (Notify)
  pDataChar =
      pService->createCharacteristic(UUID_FTMS_DATA, NIMBLE_PROPERTY::NOTIFY);

  // 2. Control Point (Write/Indicate)
  pControlChar = pService->createCharacteristic(UUID_FTMS_CONTROL_POINT,
                                                NIMBLE_PROPERTY::WRITE |
                                                    NIMBLE_PROPERTY::INDICATE);
  ControlCallbacks *controlCB = new ControlCallbacks();
  pControlChar->setCallbacks(controlCB);

  // 3. Feature (Read)
  NimBLECharacteristic *pFeature =
      pService->createCharacteristic(UUID_FTMS_FEATURE, NIMBLE_PROPERTY::READ);

  // Byte 0:
  // Bit 1: Total Distance (0x02)
  // Bit 5: Inclination (0x20)
  // Value: 0x22

  // Byte 1:
  // Bit 0 (Overall Bit 8): Expended Energy (0x01)

  // Byte 4:
  // Bit 0: Speed Target (0x01)
  // Bit 1: Incline Target (0x02)
  // Value: 0x03

  uint8_t feat[] = {0x22, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
  pFeature->setValue(feat, 8);

  // 4. Status (Notify)
  pStatusChar =
      pService->createCharacteristic(UUID_FTMS_STATUS, NIMBLE_PROPERTY::NOTIFY);

  // Link Status to Control Callbacks
  controlCB->setStatusChar(pStatusChar);

  // 5. Training Status (Required by some apps)
  // 2AD3: 00 01 (Idle)
  NimBLECharacteristic *pTrainingStatus = pService->createCharacteristic(
      UUID_FTMS_TRAINING_STATUS,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  uint8_t t_stat[] = {0x00, 0x01}; // Flags=0, Status=1(Idle)
  pTrainingStatus->setValue(t_stat, 2);

  // 6. Supported Speed Range (Mandatory for Control)
  // 0.01 KPH resolution. Min: 1.0 KPH (100), Max: 20.0 KPH (2000), Step: 0.1
  // KPH (10)
  NimBLECharacteristic *pSpeedRange = pService->createCharacteristic(
      UUID_FTMS_SPEED_RANGE, NIMBLE_PROPERTY::READ);
  uint16_t min_spd = 100;  // 1.0 KPH
  uint16_t max_spd = 2000; // 20.0 KPH
  uint16_t inc_spd = 10;   // 0.1 KPH
  uint8_t speed_range_val[] = {
      (uint8_t)(min_spd & 0xFF), (uint8_t)(min_spd >> 8),
      (uint8_t)(max_spd & 0xFF), (uint8_t)(max_spd >> 8),
      (uint8_t)(inc_spd & 0xFF), (uint8_t)(inc_spd >> 8)};
  pSpeedRange->setValue(speed_range_val, 6);

  // 7. Supported Inclination Range (Mandatory for Control)
  // 0.1% resolution. Min: -6.0% (-60), Max: 15.0% (150), Step: 1.0% (10)
  NimBLECharacteristic *pIncRange = pService->createCharacteristic(
      UUID_FTMS_INCLINE_RANGE, NIMBLE_PROPERTY::READ);
  int16_t min_inc = -60; // -6.0%
  int16_t max_inc = 150; // 15.0%
  uint16_t inc_inc = 10; // 1.0%
  uint8_t inc_range_val[] = {
      (uint8_t)(min_inc & 0xFF), (uint8_t)(min_inc >> 8),
      (uint8_t)(max_inc & 0xFF), (uint8_t)(max_inc >> 8),
      (uint8_t)(inc_inc & 0xFF), (uint8_t)(inc_inc >> 8)};
  pIncRange->setValue(inc_range_val, 6);

  pService->start();

  // ----------------------------------------------------------------
  // Device Information Service (DIS) - Essential for App Compatibility
  // ----------------------------------------------------------------
  NimBLEService *pDisService = pServer->createService("180A");

  // Manufacturer Name
  pDisService->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)
      ->setValue("iFit Bridge");

  // Model Number
  pDisService->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)
      ->setValue("Loma-1");

  // Firmware Revision
  pDisService->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)
      ->setValue("1.0.0");

  // Serial Number (2A25)
  pDisService->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)
      ->setValue("123456789");

  pDisService->start();

  // Advertisement
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(UUID_FTMS_SERVICE);
  pAdvertising->setAppearance(1344); // 0x0540 = Generic Treadmill
  pAdvertising->addTxPower();
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

  if (millis() - bridgeState.last_ftms_update > 200) { // 5Hz (Matches Python)
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

    packet[idx++] = 0xFF; // Cals/Hr L (Not Available)
    packet[idx++] = 0xFF; // Cals/Hr H (Not Available)
    packet[idx++] = 0xFF; // Energy/Min (Not Available)

    // Elapsed Time (Seconds) (2 bytes)
    uint16_t time_s = (uint16_t)bridgeState.elapsed_time_s;
    packet[idx++] = time_s & 0xFF;
    packet[idx++] = (time_s >> 8) & 0xFF;

    pDataChar->setValue(packet, idx);
    pDataChar->notify();
  }
}
