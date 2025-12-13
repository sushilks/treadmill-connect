#ifndef BRIDGE_H
#define BRIDGE_H

#include <Arduino.h>
#include <NimBLEDevice.h>

// ==========================================
// CONFIGURATION
// ==========================================
#define IFIT_DEVICE_NAME "I_TL"
#define BRIDGE_SERVER_NAME "mytm"

// ==========================================
// CONSTANTS (UUIDs)
// ==========================================
// iFit Service & Chars
static const NimBLEUUID UUID_IFIT_SERVICE(
    "00000000-0000-0000-0000-000000000000"); // Not used directly, we find by
                                             // name
static const NimBLEUUID
    UUID_IFIT_TX("00001534-1412-efde-1523-785feabcd123"); // Write
static const NimBLEUUID
    UUID_IFIT_RX("00001535-1412-efde-1523-785feabcd123"); // Notify

// FTMS Service & Chars
#define UUID_FTMS_SERVICE "1826"
#define UUID_FTMS_DATA "2ACD"
#define UUID_FTMS_CONTROL_POINT "2AD9"
#define UUID_FTMS_FEATURE "2ACC"
#define UUID_FTMS_STATUS "2ADA"
#define UUID_FTMS_SPEED_RANGE "2AD4"
#define UUID_FTMS_INCLINE_RANGE "2AD5"
#define UUID_FTMS_TRAINING_STATUS "2AD3"

// ==========================================
// SHARED STATE
// ==========================================
struct BridgeState {
  bool connected_to_ifit = false;
  bool connected_to_ftms = false;
  bool client_scanning = false;
  bool server_advertising = false;

  // Telemetry
  double speed_kph = 0.0;
  double incline_pct = 0.0;
  uint32_t distance_m = 0;
  uint32_t elapsed_time_s = 0;
  uint32_t calories = 0;

  // Last Update Times
  uint32_t last_ftms_update = 0;

  // Control Queues (Simple flags for now, can be queues later)
  bool pending_control = false;
  uint8_t pending_control_type = 0; // 0=None, 1=Speed, 2=Incline
  int16_t pending_control_value = 0;
};

extern BridgeState bridgeState;

#endif
