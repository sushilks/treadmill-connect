# iFit Protocol Packet Analysis Sample

This document provides a detailed, byte-by-byte analysis of a captured `SupportedCapabilities` exchange between the App and an iFit Treadmill. It breaks down the full protocol stack from the HCI layer down to the iFit payload.

## 1. Command Packet (TX)
**Timestamp:** `09:13:58.905`
**Raw File Bytes:** `02 47 00 1B 00 17 00 04 00 12 0E 00 FF 08 02 04 02 04 04 04 80 88`

### Layer 1: HCI ACL Header
| Bytes | Hex | Value | Description |
| :--- | :--- | :--- | :--- |
| **0** | `02` | `0x02` | **Packet Type** (`ACL Data Out` / TX) |
| **1-2** | `47 00` | `0x0047` | **Connection Handle** (Little Endian, handle 0x47) |
| **3-4** | `1B 00` | `27` | **Total Data Length** (27 bytes follow) |

### Layer 2: L2CAP Header
| Bytes | Hex | Value | Description |
| :--- | :--- | :--- | :--- |
| **0-1** | `17 00` | `23` | **L2CAP Length** (23 bytes follow) |
| **2-3** | `04 00` | `0x0004` | **Channel ID** (`Attribute Protocol`) |

### Layer 3: ATT Protocol
| Bytes | Hex | Value | Description |
| :--- | :--- | :--- | :--- |
| **0** | `12` | `0x12` | **Opcode** (`Write Request`) |
| **1-2** | `0E 00` | `0x000E` | **Attribute Handle** (Target Characteristic) |

### Layer 4: BLE Payload (iFit Protocol)
| Bytes | Hex | Value | Description |
| :--- | :--- | :--- | :--- |
| **0** | `FF` | `255` | **Wrapper/Flag** (Unchunked Message Marker) |
| **1** | `08` | `8` | **Length** (8 bytes follow) |
| **2-4** | `02 04 02` | - | **Header** (Standard Request Header) |
| **5** | `04` | `4` | **Dest Equip** (Treadmill) |
| **6** | `04` | `4` | **Length** (Payload Length+1 for Checksum) |
| **7** | `04` | `4` | **Src Equip** (App) |
| **8** | `80` | `0x80` | **Command** (`SupportedCapabilities`) |
| **9** | `88` | `0x88` | **Checksum** (4+4+128 = 136) |

---

## 2. Response Packets (RX) - Chunked
**Timestamp:** `09:13:58.963 - 09:13:58.994`
The response is split into 3 packets because it exceeds the BLE MTU size.

### Packet 1: Metadata / Header Chunk
**Raw ATT Payload:** `FE 02 14 03 DB FC FB 3B 0C E7 1F 00 C0`

| Byte | Hex | Description |
| :--- | :--- | :--- |
| **0** | `FE` | **Start of Chunked Message** (Marker) |
| **1** | `02` | **Flags** |
| **2** | `14` | **Total Message Length** (20 bytes of actual data to follow in subsequent chunks) |
| **3** | `03` | **Total Packets** (1 Meta + 2 Data = 3 packets) |
| **4-12**| `...` | **Padding/Ignored** (Reserved checksums/overhead, unused for payload assembly) |

### Packet 2: Data Chunk #0
**Raw ATT Payload:** `00 12 01 04 02 10 04 10 80 02 0A 4C 47 ...`

| Byte | Hex | Description |
| :--- | :--- | :--- |
| **0** | `00` | **Sequence Index** (0 = First Data Chunk) |
| **1** | `12` | **Chunk Length** (18 bytes of valid data follow) |
| **2-19**| `...` | **Data** (First 18 bytes of the iFit Message) |

### Packet 3: Data Chunk #1 (EOF)
**Raw ATT Payload:** `FF 02 42 77`

| Byte | Hex | Description |
| :--- | :--- | :--- |
| **0** | `FF` | **Sequence Index** (255 = End of File / Last Chunk) |
| **1** | `02` | **Chunk Length** (2 bytes of valid data follow) |
| **2** | `42` | **Data Byte** (Part of payload) |
| **3** | `77` | **Checksum** (Last byte of message) |
| **4+** | - | **Ignored** (Any subsequent bytes are ignored because Length was 2) |

---

## 3. Reassembled Response (Full iFit Message)
Combining the data from Packet 2 (18 bytes) and Packet 3 (2 bytes):
**Hex:** `01 04 02 10 04 10 80 02 0A 4C 47 4D 4E 40 46 4F 51 41 42 77`

| Byte | Hex | Value | Description |
| :--- | :--- | :--- | :--- |
| **0-2** | `01 04 02` | - | **Header** (Response Header) |
| **3** | `10` | `16` | **Length** (16 bytes payload follow) |
| **4** | `04` | `4` | **Src Equip** (Treadmill) |
| **5** | `10` | `16` | **Dest Equip** (App) |
| **6** | `80` | `128` | **Command** (`SupportedCapabilities` Response) |
| **7** | `02` | `2` | **Start Flag** |
| **8** | `0A` | `10` | **Count** (10 Capabilities) |
| **9-18**| `...` | - | **Capability Codes** (List of 10 bytes) |
| **19** | `77` | `119` | **Checksum** |
