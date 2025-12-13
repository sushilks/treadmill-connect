# iFit Treadmill Control Protocol Findings (VERIFIED)

## 1. Control Endpoint
- **Equipment ID**: `0x09` (Motor Controller)
- **Command**: `0x02` (WriteAndRead)
- **Base Header**: `02 04 02 09 04 09 02 01` (Common prefix for writing values)

## 2. Initialization & Safety Handshake (MANDATORY)
Before any motor control commands are accepted, the following "Enable" sequence must be sent. Failure to do so results in the treadmill ignoring commands or disconnecting.

**Critical Steps:**
1.  **Device Discovery**: Equipment Information (`0x81`), Capabilities (`0x80`), etc.
2.  **(Key Step) Enable Command**: Command `0x90` with a specific payload (likely authentication/unlock).
    - **Header**: `02 04 02` ... `90`
    - **Payload**: `07 01 8D 68 49 28...` (Fixed static payload from app capture)
3.  **Start Stream**: Command `0x02` to Equip `0x13` (Telemetry Start).

## 3. Speed Control (VERIFIED)
- **Type ID**: `01`
- **Scaling Factor**: `100` (Raw Value = KPH * 100)
- **Structure**: `02 04 02 09 04 09 02 01 01 [Val_Low] [Val_High] 00`

**Example: Set Speed to 4.0 KPH**
- Value: `4.0 * 100 = 400 = 0x0190`
- bytes: `90 01`
- **Packet**: `02 04 02 09 04 09 02 01 01 90 01 00 [Checksum]`

## 4. Incline Control (VERIFIED)
- **Type ID**: `02`
- **Scaling Factor**: `60` (Raw Value = % * 60)
- **Structure**: `02 04 02 09 04 09 02 01 02 [Val_Low] [Val_High] 00 00` (Note: Extra padding byte compared to speed, or just variable length)

**Example: Set Incline to 2.0%**
- Value: `2.0 * 60 = 120 = 0x0078`
- bytes: `78 00`
- **Packet**: `02 04 02 09 04 09 02 01 02 78 00 00 [Checksum]`

## 5. Checksum Algorithm
Sum of all bytes starting from the `Length` byte (index 4) up to the end of the payload.
`Checksum = Sum(Length + SrcEquip + Command + Payload_Bytes) & 0xFF`

## 6. Telemetry / Status Reading (VERIFIED)
To read the current state, poll Equipment `0x10` (General/Console) with Command `0x02`. The treadmill typically fails if you don't poll it regularly.

- **Poll Command**: `02 04 02 10 04 10 02 00 ...` (See `read_telemetry.py` for exact payload).
- **Response Source**: Equipment `0x2F` (47).
- **Response Structure**: `01 04 02 2F 04 2F ...`

**Data Offsets (Little Endian):**
- **Target Speed**: Offset 8 (2 bytes). Scale 100.
  - `KPH = Value / 100.0`
- **Actual Speed**: Offset 10 (2 bytes). Scale 100.
  - `KPH = Value / 100.0`
- **Incline**: Offset 28 (2 bytes). Scale 60 (Wait, confirmed as Scale 100 in Read?).
  - **Correction**: In `read_telemetry.py`, we found `0x00C8` (200) for 2.0% Incline.
  - **Read Scaling**: 100 (Value = % * 100).
  - **Write Scaling**: 60 (Value = % * 60).
  - *Note the difference between Read and Write scaling for Incline!*
