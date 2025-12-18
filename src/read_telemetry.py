
import asyncio
from bleak import BleakClient, BleakScanner
import struct
import sys

import logging

# Configure Logging
# logging.basicConfig(level=logging.INFO)
# logger = logging.getLogger("bleak")
# logger.setLevel(logging.DEBUG) # Catch low level BLE events

DEVICE_NAME = "I_TL"
# Alternate UUIDs (Service 1530-1212...)
# WRITE_CHAR_UUID = "00001532-1212-efde-1523-785feabcd123" # Write Without Response
# NOTIFY_CHAR_UUID = "00001531-1212-efde-1523-785feabcd123" # Notify/Write
WRITE_CHAR_UUID = "00001534-1412-efde-1523-785feabcd123"
NOTIFY_CHAR_UUID = "00001535-1412-efde-1523-785feabcd123"

# --- Initialization & Poll Commands ---
CMD_1_INFO = bytes.fromhex("0204020402048187")
CMD_2_CAPS = bytes.fromhex("0204020404048088")
CMD_3_CMDS = bytes.fromhex("0204020404048890")
CMD_4_INFO2 = bytes.fromhex("020402070207820000008B")
CMD_5_INFO3 = bytes.fromhex("0204020602068400008C")
CMD_6_95 = bytes.fromhex("020402040204959B")
CMD_7_ENABLE = bytes.fromhex("0204022804289007018D68492815F0E9C0BDA89988756079704D484948757069609D88B9A8D5C0A0020000AD")
CMD_8_UNK = bytes.fromhex("020402150415020E000000000000000000000000001001003A")
CMD_9_START = bytes.fromhex("020402130413020C0000000000000000000000800000A5")
CMD_POLL_STATUS = bytes.fromhex("02040210041002000A13943300104010008018F2")

class PacketReassembler:
    def __init__(self):
        self.buffer = bytearray()
        self.expected_length = 0

    def process_chunk(self, data):
        payloads = []
        if data[0] == 0xFE and len(data) >= 4:
            self.expected_length = data[2]
            self.buffer = bytearray()
            return []
        if len(data) > 2:
            self.buffer.extend(data[2:2+data[1]])
            if data[0] == 0xFF:
                if len(self.buffer) == self.expected_length:
                    payloads.append(bytes(self.buffer))
                self.buffer = bytearray()
        return payloads

reassembler = PacketReassembler()

global START_DIST
START_DIST = None

def decode_status(payload):
    global START_DIST
    # Expecting: 01 04 02 2F 04 2F ...
    if len(payload) < 30 or payload[3] != 0x2F:
        return

    try:
        # Offsets...
        # Distance (Offset 42) - Cumulative Meters (Scale 100cm)
        if len(payload) >= 42 + 4:
            dist_raw = struct.unpack_from('<I', payload, 42)[0]
            total_km = dist_raw / 100.0 / 1000.0
            total_mi = total_km * 0.621371
        else:
            total_mi = 0.0

        # ... other decoding ...
        # Speed (8), Incline (10), Time (27), Calories (31)
        speed_raw = struct.unpack_from('<H', payload, 8)[0]
        speed_mph = (speed_raw / 100.0) * 0.621371
        
        incline_raw = struct.unpack_from('<H', payload, 10)[0]
        incline_pct = incline_raw / 100.0
        
        time_sec = struct.unpack_from('<I', payload, 27)[0]
        m, s = divmod(time_sec, 60)
        h, m = divmod(m, 60)
        
        cal_raw = struct.unpack_from('<I', payload, 31)[0]
        cal_val = cal_raw / 97656.0

        output = (f"\r🏃 {speed_mph:4.1f} MPH | ⛰️  {incline_pct:4.1f}% | "
                  f"⏱️  {h:02}:{m:02}:{s:02} | 📏 {total_mi:6.3f} mi | 🔥 {cal_val:4.1f} cal")
                  
        if DEBUG_MODE:
             print(f"\nRAW: {payload.hex().upper()}")
             print(output.strip()) 
        else:
             print(output, end="")
        
        sys.stdout.flush()
        
    except Exception as e:
        if DEBUG_MODE: print(f"Error: {e}")

def notification_handler(sender, data):
    # Notify that we got data
    # print(f"[RAW] {data.hex()}")
    # Check for Reassembly
    # Debug: Confirm verify we get ANY data
    if DEBUG_MODE: print(f"RX: {data.hex().upper()}") # Debug
    for p in reassembler.process_chunk(data):
        decode_status(p)

async def send_chunked_message(client, payload, char_obj=None):
    total_len = len(payload)
    chunks = []
    data_slices = [payload[i:i+18] for i in range(0, total_len, 18)]
    total_chunks = 1 + len(data_slices)
    
    header = bytearray([0xFE, 0x02, total_len, total_chunks]) + b'\x00'*16
    chunks.append(header)
    
    for i, chunk in enumerate(data_slices):
        seq = 0xFF if i == len(data_slices)-1 else i
        chunks.append(bytearray([seq, len(chunk)]) + chunk)
        
    for pkt in chunks:
        target = char_obj if char_obj else WRITE_CHAR_UUID
        if DEBUG_MODE: print(f"TX: {pkt.hex().upper()}") # Debug TX
        # Add timeout to prevent indefinite hang
        try:
             await asyncio.wait_for(client.write_gatt_char(target, pkt), timeout=5.0)
        except asyncio.TimeoutError:
             logger.error("Write Timeout! Device stuck?")
             raise
        await asyncio.sleep(0.1) # SLOWER: 0.1 for stability

async def main():
    import argparse
    parser = argparse.ArgumentParser(description='Monitor iFit Treadmill Telemetry')
    parser.add_argument('--debug', action='store_true', help='Show raw hex packets')
    args = parser.parse_args()
    
    global DEBUG_MODE
    DEBUG_MODE = args.debug

    print(f"Scanning for {DEVICE_NAME}...")
    device = None
    # device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    devices = await BleakScanner.discover(timeout=10.0)
    device = next((d for d in devices if d.name == DEVICE_NAME), None)
    if not device: 
        print("Device not found.")
        return

    print(f"Connecting to {device.address}...")
    async with BleakClient(device) as client:
        print("Connected. Resolving Services...")
        # await client.get_services() # Removed in Bleak 2.0.0

        
        
        # Always Print Services for Debugging
        print("Discovered Services:")
        for service in client.services:
            print(f"Service: {service.uuid}")
            for char in service.characteristics:
                    print(f"  Char: {char.uuid} ({char.properties})")
        
        await client.start_notify(NOTIFY_CHAR_UUID, notification_handler)
        
        # Find the Characteristic OBJECT to avoid lookup errors
        write_char = client.services.get_characteristic(WRITE_CHAR_UUID)
        if not write_char:
            print(f"Critical Error: Could not find Write Char {WRITE_CHAR_UUID}")
            return
            
        print(f"Found Write Char Handle: {write_char.handle}")

        # Initialization Sequence
        init_cmds = [
            CMD_1_INFO, CMD_2_CAPS, CMD_3_CMDS, CMD_4_INFO2, 
            CMD_5_INFO3, CMD_6_95, CMD_7_ENABLE, CMD_8_UNK, CMD_9_START
        ]
        
        print("Starting Handshake (Listening for responses)...")
        for i, cmd in enumerate(init_cmds):
            if DEBUG_MODE: print(f"Sending Handshake CMD {i+1}/{len(init_cmds)}...")
            await send_chunked_message(client, cmd, write_char)
            # Give time for response (Match ifit-ctrl.py)
            if cmd == CMD_7_ENABLE:
                print("Enabling... waiting 2s")
                await asyncio.sleep(2.0) # Increased from 0.5s
            elif cmd == CMD_8_UNK:
                await asyncio.sleep(0.5)
            elif cmd == CMD_9_START:
                await asyncio.sleep(1.0)
            else:
                await asyncio.sleep(0.1)
            
        print("\nHandshake Complete.")
        print("="*60)
 
        print(" iFit Telemetry Monitor")
        print("="*60)
        
        while True:
            # print(".", end="", flush=True) # Heartbeat
            await send_chunked_message(client, CMD_POLL_STATUS, write_char)
            await asyncio.sleep(0.2)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped.")
