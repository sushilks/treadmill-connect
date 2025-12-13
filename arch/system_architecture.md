# System Architecture

## Overview
The **Treadmill Connect** system acts as a "Man-in-the-Middle" bridge. It solves the incompatibility issue by presenting two different Bluetooth faces:
1.  **Client Face**: Connects to the iFit treadmill (mimicking the capabilities of the official iFit tablet app).
2.  **Server Face**: Advertises as a standard FTMS treadmill (mimicking a generic smart treadmill).

## High-Level Diagram

```text
     +-----------------+                                 +------------------+
     |                 |                                 |                  |
     | iFit Treadmill  |                                 |   User Phone     |
     | (Proprietary)   |                                 | (Zwift / Runna)  |
     |                 |                                 |                  |
     +--------+--------+                                 +---------+--------+
              |                                                    ^
              | 1. Proprietary                                     | 2. Standard
              |    Connection                                      |    FTMS BLE
              v                                                    v
+-------------+----------------------------------------------------+------------+
|                          TREADMILL CONNECT BRIDGE                             |
|                           (Laptop / ESP32)                                    |
|                                                                               |
|  +----------------+        +-------------------+        +------------------+  |
|  |                |        |                   |        |                  |  |
|  |  iFit Client   | <----> |  Protocol Logic   | <----> |   FTMS Server    |  |
|  |    (bleak)     |        |   (Translator)    |        |     (bless)      |  |
|  |                |        |                   |        |                  |  |
|  +----------------+        +-------------------+        +------------------+  |
|                                                                               |
+-------------------------------------------------------------------------------+

Data Flow:
1. Treadmill dumps raw hex data -> Client reads it.
2. Translator decodes hex -> Metric Units (Speed/Incline).
3. FTMS Server updates characteristics -> Phone reads standard values.
4. Phone writes Target Speed -> Server receives it.
5. Translator scales it back to raw hex -> Client writes to Treadmill.
```

## Component Roles

### 1. iFit Treadmill (The Hardware)
*   **Role**: Peripheral (Server).
*   **Behavior**: Broadcasts as `I_TL`. Waits for a specific 9-step write sequence to "unlock" the motor controller.
*   **Data**: Sends proprietary hex dumps containing raw speed, incline, and calorie ticks.

### 2. The Bridge (Laptop / ESP32)
*   **Role**: Dual-Mode (Client & Server).
*   **Component A: iFit Client**:
    *   Finds `I_TL`.
    *   Performs the generic handshake (`FE02...` packets).
    *   Keeps the connection alive with 1Hz polls.
*   **Component B: Translator**:
    *   Decodes raw iFit bytes -> Metric Units.
    *   Encodes FTMS commands -> iFit raw bytes.
*   **Component C: FTMS Server**:
    *   Advertises as `loma` (or `Treadmill`).
    *   Exposes standard Bluetooth Services (`1826` - Fitness Machine).

### 3. User Phone (The App)
*   **Role**: Central (Client).
*   **Behavior**: Scans for FTMS devices.
*   **Apps**: Runna, Zwift, Kinomap, etc.
*   **Action**: Reads speed/distance from the bridge; sends target speed/incline to the bridge.

## Data Flow Example

### Scenario 1: Reading Speed
1.  **Treadmill** belt moves.
2.  **Treadmill** sends proprietary packet `...0500...` (Speed = 500).
3.  **Bridge Client** receives packet.
4.  **Translator** divides by 100 -> `5.0 km/h`.
5.  **FTMS Server** updates `Treadmill Data` characteristic with `5.0`.
6.  **Runna App** receives notification: "Speed is 5.0 km/h".

### Scenario 2: Controlling Incline
1.  **User** hits "5%" incline on **Zwift** (or Zwift hits a hill).
2.  **Zwift** sends standard FTMS Write: `OpCode=0x03, Value=50` (5.0%).
3.  **FTMS Server** receives write command.
4.  **Translator** scales value (x10) -> `500`.
5.  **Bridge Client** constructs iFit packet: `FE02...020102...` (Set Incline).
6.  **Treadmill** receives command and lifts the deck.
