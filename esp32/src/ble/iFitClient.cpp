#include "iFitClient.h"

static int scanTime = 5; // Scan for 5 seconds

// =================================================================================
// HANDSHAKE COMMANDS (Derived from main.py)
// =================================================================================
const uint8_t CMD_1[] = {0x02, 0x04, 0x02, 0x04, 0x02, 0x04, 0x81, 0x87};
const uint8_t CMD_2[] = {0x02, 0x04, 0x02, 0x04, 0x04, 0x04, 0x80, 0x88};
const uint8_t CMD_3[] = {0x02, 0x04, 0x02, 0x04, 0x04, 0x04, 0x88, 0x90};
const uint8_t CMD_4[] = {0x02, 0x04, 0x02, 0x07, 0x02, 0x07,
                         0x82, 0x00, 0x00, 0x00, 0x8B};
const uint8_t CMD_5[] = {0x02, 0x04, 0x02, 0x06, 0x02,
                         0x06, 0x84, 0x00, 0x00, 0x8C};
const uint8_t CMD_6[] = {0x02, 0x04, 0x02, 0x04, 0x02, 0x04, 0x95, 0x9B};
const uint8_t CMD_7[] = {0x02, 0x04, 0x02, 0x28, 0x04, 0x28, 0x90, 0x07, 0x01,
                         0x8D, 0x68, 0x49, 0x28, 0x15, 0xF0, 0xE9, 0xC0, 0xBD,
                         0xA8, 0x99, 0x88, 0x75, 0x60, 0x79, 0x70, 0x4D, 0x48,
                         0x49, 0x48, 0x75, 0x70, 0x69, 0x60, 0x9D, 0x88, 0xB9,
                         0xA8, 0xD5, 0xC0, 0xA0, 0x02, 0x00, 0x00, 0xAD};
const uint8_t CMD_8[] = {0x02, 0x04, 0x02, 0x15, 0x04, 0x15, 0x02, 0x0E,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x3A};
const uint8_t CMD_9[] = {0x02, 0x04, 0x02, 0x13, 0x04, 0x13, 0x02,
                         0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0xA5};

const uint8_t CMD_POLL[] = {0x02, 0x04, 0x02, 0x10, 0x04, 0x10, 0x02,
                            0x00, 0x0A, 0x13, 0x94, 0x33, 0x00, 0x10,
                            0x40, 0x10, 0x00, 0x80, 0x18, 0xF2};

// =================================================================================
// ADVERTISED DEVICE CALLBACKS
// =================================================================================
class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  iFitClient *text_client;

public:
  AdvertisedDeviceCallbacks(iFitClient *client) : text_client(client) {}

  void onResult(NimBLEAdvertisedDevice *advertisedDevice) {
    if (advertisedDevice->getName() == IFIT_DEVICE_NAME) {
      Serial.println("Found iFit Treadmill!");
      advertisedDevice->getScan()->stop();
      text_client->onDeviceFound(advertisedDevice);
    }
  }
};

// =================================================================================
// IMPLEMENTATION
// =================================================================================

void iFitClient::init() {
  pClient = nullptr;
  pDevice = nullptr;
  pTxChar = nullptr;
  pRxChar = nullptr;

  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(this));
  pScan->setInterval(45);
  pScan->setWindow(15);
  pScan->setActiveScan(true);
}

void iFitClient::onDeviceFound(NimBLEAdvertisedDevice *device) {
  pDevice = device;
}

void iFitClient::loop() {
  // 1. If not connected, try to find or connect
  if (!bridgeState.connected_to_ifit) {

    // If we found a device, try to connect
    if (pDevice != nullptr) {
      if (connect()) {
        Serial.println("Connected to Treadmill!");
        performHandshake();
        Serial.println("Handshake Complete.");
        bridgeState.connected_to_ifit = true;
      } else {
        Serial.println("Failed to connect. Restarting scan...");
        pDevice = nullptr; // Reset to scan again
      }
      return;
    }

    // Otherwise scan periodically
    if (millis() - last_poll_ms > 10000) { // Scan every 10s if not found
      last_poll_ms = millis();
      if (!NimBLEDevice::getScan()->isScanning()) {
        Serial.println("Starting BLE Scan...");
        NimBLEDevice::getScan()->start(scanTime, false);
      }
    }
  } else {
    // Connected Logic
    if (pClient && !pClient->isConnected()) {
      Serial.println("Disconnected from Treadmill (Physical Device).");
      bridgeState.connected_to_ifit = false;
      pDevice = nullptr;
      NimBLEDevice::deleteClient(pClient);
      pClient = nullptr;
      return;
    }

    // Polling Loop
    // Polling Loop (Keep-Alive)
    // Python only polls if no telemetry for 1.0s.
    // We check last_rx_ms.
    // Also check pending control - always send control immediately.

    if (bridgeState.pending_control) {
      sendControlCommand(bridgeState.pending_control_type,
                         (uint16_t)bridgeState.pending_control_value);
      bridgeState.pending_control = false;
      // Don't poll immediately after control
      last_poll_ms = millis();
    }
    // Only poll if connected and silence > 1000ms
    else if (pTxChar && (millis() - last_rx_ms > 1000)) {
      if (millis() - last_poll_ms > 1000) { // Limit poll rate to 1Hz if silence
        last_poll_ms = millis();
        sendChunked(CMD_POLL, sizeof(CMD_POLL));
      }
    }
  }
}

void iFitClient::sendControlCommand(uint8_t type, uint16_t value) {
  if (!pTxChar)
    return;

  // Based on main.py create_control_command
  uint8_t cmd[22];
  // Header? No, create_control_command returns raw bytes. sendChunked adds
  // header. Cmd Struct: 02 04 02 09 04 09 02 01 [TYPE] [VAL_L] [VAL_H] 00 [CS]
  // Base: 0204020904090201
  uint8_t base[] = {0x02, 0x04, 0x02, 0x09, 0x04, 0x09, 0x02, 0x01};

  int idx = 0;
  memcpy(cmd, base, 8);
  idx += 8;
  cmd[idx++] = type; // 0x01 Speed, 0x02 Incline
  cmd[idx++] = value & 0xFF;
  cmd[idx++] = (value >> 8) & 0xFF;
  cmd[idx++] = 0x00;

  // Checksum: Sum of bytes from index 4 to end (excluding CS itself)
  // base[4] is index 4.
  // 04 09 02 01 TYPE V_L V_H 00
  uint16_t sum = 0;
  for (int i = 4; i < idx; i++) {
    sum += cmd[i];
  }
  cmd[idx++] = sum & 0xFF;

  Serial.printf("Sending Control: Type=%d Val=%d\n", type, value);
  sendChunked(cmd, idx);
}

bool iFitClient::connect() {
  if (!pDevice)
    return false;

  pClient = NimBLEDevice::createClient();
  if (!pClient->connect(pDevice)) {
    NimBLEDevice::deleteClient(pClient);
    pClient = nullptr;
    return false;
  }

  // Get Service
  NimBLERemoteService *pSvc = pClient->getService(IFIT_SERVICE_UUID);
  if (!pSvc) {
    Serial.println("Failed to find service");
    pClient->disconnect();
    return false;
  }

  // Get Characteristics
  pTxChar = pSvc->getCharacteristic(IFIT_TX_UUID);
  pRxChar = pSvc->getCharacteristic(IFIT_RX_UUID);

  if (!pTxChar || !pRxChar) {
    Serial.println("Failed to find characteristics");
    pClient->disconnect();
    return false;
  }

  // Subscribe to Notify
  if (pRxChar->canNotify()) {
    // Cleanest way in NimBLE CPP: pass 'this' via a lambda or simple static
    // Since we defined onResult as static in the class:
    pRxChar->subscribe(true, onResult);
  }

  return true;
}

void iFitClient::performHandshake() {
  if (!pTxChar)
    return;

  Serial.println("Performing Handshake...");

  const uint8_t *cmds[] = {CMD_1, CMD_2, CMD_3, CMD_4, CMD_5,
                           CMD_6, CMD_7, CMD_8, CMD_9};
  size_t lens[] = {sizeof(CMD_1), sizeof(CMD_2), sizeof(CMD_3),
                   sizeof(CMD_4), sizeof(CMD_5), sizeof(CMD_6),
                   sizeof(CMD_7), sizeof(CMD_8), sizeof(CMD_9)};

  // Extra Commands from Python (HS_STEPS 10-12)
  const uint8_t CMD_10[] = {0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x3A};
  const uint8_t
      CMD_11[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // 23 bytes of 0s
  const uint8_t CMD_12[] = {0x00, 0x00, 0x80, 0x00, 0x00, 0xA5};

  for (int i = 0; i < 9; i++) {
    sendChunked(cmds[i], lens[i]);
    if (i == 6 || i == 7)
      delay(500); // 7 and 8 need more time
    else if (i == 8)
      delay(2000); // Step 9 (CMD_8/9) delay
    else
      delay(100);
  }
}

void iFitClient::sendChunked(const uint8_t *data, size_t length) {
  if (!pTxChar)
    return;

  uint8_t buffer[22];

  size_t chunk_count = (length + 17) / 18;
  size_t total_chunks = 1 + chunk_count;

  // Header: FE 02 [Len] [TotalChunks] ...
  buffer[0] = 0xFE;
  buffer[1] = 0x02;
  buffer[2] = (uint8_t)length;
  buffer[3] = (uint8_t)total_chunks;
  memset(buffer + 4, 0, 16);

  // Use Write With Response (true) to ensure delivery
  // Increase delay to 100ms to match Python's 0.1s
  pTxChar->writeValue(buffer, 20, true);
  delay(100);

  for (size_t i = 0; i < chunk_count; i++) {
    size_t offset = i * 18;
    size_t remaining = length - offset;
    size_t this_len = (remaining > 18) ? 18 : remaining;

    uint8_t seq = (i == chunk_count - 1) ? 0xFF : i;

    buffer[0] = seq;
    buffer[1] = (uint8_t)this_len;
    memcpy(buffer + 2, data + offset, this_len);

    pTxChar->writeValue(buffer, 2 + this_len, true);
    delay(100);
  }
}

void iFitClient::onResult(NimBLERemoteCharacteristic *pBLERemoteCharacteristic,
                          uint8_t *pData, size_t length, bool isNotify) {
  // We need to access the client instance.
  // Since this is static, we can't access 'this'.
  // But since we have a singleton 'ifitClient' defined in main.cpp, we can use
  // 'extern' or just handle it here if we assume 1 instance. Ideally, we'd pass
  // a context, but NimBLE C++ callback signature is fixed. Workaround: Access
  // the global 'ifitClient' object.
  extern iFitClient ifitClient;

  // Track last RX time for silence-based polling
  ifitClient.last_rx_ms = millis();

  // DEBUG: Hex Dump every RX packet (Keep for now to verify fix)
  Serial.printf("RX (Len=%d): ", length);
  for (size_t i = 0; i < length; i++) {
    Serial.printf("%02X ", pData[i]);
  }
  Serial.println();

  ifitClient.processTelemetry(pData, length);
}

void iFitClient::processTelemetry(uint8_t *data, size_t len) {
  if (len < 2)
    return;

  uint8_t seq = data[0];

  if (seq == 0xFE) {
    // Start of message
    reassembly_len = 0;
    reassembly_in_progress = true;
  } else if (reassembly_in_progress) {
    uint8_t chunk_len = data[1];
    if (reassembly_len + chunk_len < sizeof(reassembly_buffer)) {
      memcpy(reassembly_buffer + reassembly_len, data + 2, chunk_len);
      reassembly_len += chunk_len;
    }

    if (seq == 0xFF) {
      // End of message
      reassembly_in_progress = false;

      // Check Payload Source (0x2F at index 3 in main packet)
      // Payload Start is index 0 of reassembled buffer
      // 02 04 02 2F ...
      if (reassembly_len > 10 && reassembly_buffer[3] == 0x2F) { // 0x2F = 47
        // Parse Logic
        // Parse Logic
        // Speed: Offset 8 (uint16)
        uint16_t speed_raw = reassembly_buffer[8] | (reassembly_buffer[9] << 8);
        bridgeState.speed_kph = speed_raw / 100.0f;

        // Incline: Offset 10 (uint16)
        uint16_t inc_raw = reassembly_buffer[10] | (reassembly_buffer[11] << 8);
        bridgeState.incline_pct = inc_raw / 100.0f;

        // Distance: Offset 42 (uint32) / 100.0
        // Raw is Meters? Or KM?
        // Logic: 195 -> 1.95. If 1.95km -> 1950 meters.
        // User saw 1.2m on phone when we sent 1.
        // Let's assume raw is 100x KM.
        // So dist_raw=195 => 1.95km.
        // bridgeState.distance_m needs METERS.
        // So 1.95 * 1000 = 1950.
        // dist_raw (195) / 100 * 1000 = dist_raw * 10.

        if (reassembly_len >= 46) {
          uint32_t dist_raw =
              reassembly_buffer[42] | (reassembly_buffer[43] << 8) |
              (reassembly_buffer[44] << 16) | (reassembly_buffer[45] << 24);
          // Raw is Centimeters (Metric). Convert to Meters.
          bridgeState.distance_m = dist_raw / 100;
        }

        // Time: Offset 27 (uint32) - Appears to be Seconds, not Milliseconds
        if (reassembly_len >= 31) {
          uint32_t time_raw =
              reassembly_buffer[27] | (reassembly_buffer[28] << 8) |
              (reassembly_buffer[29] << 16) | (reassembly_buffer[30] << 24);
          bridgeState.elapsed_time_s = time_raw;
        }

        // Calories: Offset 31 (uint32) / 97656.0
        if (reassembly_len >= 35) {
          uint32_t cal_raw =
              reassembly_buffer[31] | (reassembly_buffer[32] << 8) |
              (reassembly_buffer[33] << 16) | (reassembly_buffer[34] << 24);
          bridgeState.calories = cal_raw / 97656.0f;
        }

        // Check for changes and log immediately
        static float last_spd = -1.0f;
        static float last_inc = -999.0f;

        if (abs(bridgeState.speed_kph - last_spd) > 0.1f ||
            abs(bridgeState.incline_pct - last_inc) > 0.1f) {

          Serial.printf("Telem Update: Spd=%.1f KPH Inc=%.1f%% (Dist=%.1f)\n",
                        bridgeState.speed_kph, bridgeState.incline_pct,
                        bridgeState.distance_m);

          last_spd = bridgeState.speed_kph;
          last_inc = bridgeState.incline_pct;
        }

        // Keep the periodic log for heartbeat, but make it less frequent (10s)
        static uint32_t last_telem_print = 0;
        if (millis() - last_telem_print > 10000) {
          last_telem_print = millis();
          Serial.printf("Telem Heartbeat: Spd=%.1f KPH Inc=%.1f%%\n",
                        bridgeState.speed_kph, bridgeState.incline_pct);
        }
      }
    }
  }
}
