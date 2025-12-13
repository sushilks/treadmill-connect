#ifndef IFIT_CLIENT_H
#define IFIT_CLIENT_H

#include "Bridge.h"
#include <NimBLEDevice.h>

// UUIDs
#define IFIT_SERVICE_UUID "00001533-1412-efde-1523-785feabcd123"
#define IFIT_TX_UUID "00001534-1412-efde-1523-785feabcd123"
#define IFIT_RX_UUID "00001535-1412-efde-1523-785feabcd123"

class iFitClient {
public:
  void init();
  void loop();
  void onDeviceFound(NimBLEAdvertisedDevice *device);

private:
  NimBLEClient *pClient;
  NimBLEAdvertisedDevice *pDevice;
  NimBLERemoteCharacteristic *pTxChar;
  NimBLERemoteCharacteristic *pRxChar;

  // Handshake
  bool connect();
  void performHandshake();
  void sendChunked(const uint8_t *data, size_t length);
  void sendControlCommand(uint8_t type, uint16_t value);

  // Telemetry
  void processTelemetry(uint8_t *data, size_t len);
  static void onResult(NimBLERemoteCharacteristic *pBLERemoteCharacteristic,
                       uint8_t *pData, size_t length, bool isNotify);

  // State
  uint32_t last_poll_ms = 0;

  // Reassembly
  uint8_t reassembly_buffer[512];
  size_t reassembly_len = 0;
  bool reassembly_in_progress = false;
};

#endif
