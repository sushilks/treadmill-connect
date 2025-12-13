#!/usr/bin/env python3
import asyncio
import struct
import sys
import time
import argparse
import tty
import termios
from bleak import BleakClient, BleakScanner

# =============================================================================
# CONSTANTS & PROTOCOL
# =============================================================================
DEVICE_NAME = "I_TL"
UUID_TX = "00001534-1412-efde-1523-785feabcd123"  # Write
UUID_RX = "00001535-1412-efde-1523-785feabcd123"  # Notify

TYPE_SPEED = 0x01
TYPE_INCLINE = 0x02
POLL_CMD = bytes.fromhex("02040210041002000A13943300104010008018F2")

# Handshake Sequence (From verify_speed.py)
HS_STEPS = [
    ("FE02080200000000000000000000000000000000", "FF080204020402048187"),
    ("FE02080200000000000000000000000000000000", "FF080204020404048088"),
    ("FE02080200000000000000000000000000000000", "FF080204020404048890"),
    ("FE020B0200000000000000000000000000000000", "FF0B020402070207820000008B"),
    ("FE020A0200000000000000000000000000000000", "FF0A0204020602068400008C"),
    ("FE02080200000000000000000000000000000000", "FF08020402040204959B"),
    ("FE022C0400000000000000000000000000000000", "00120204022804289007018D68492815F0E9C0BD"),
    ("0112A89988756079704D484948757069609D88B9", "FF08A8D5C0A0020000AD"),
    ("FE02190300000000000000000000000000000000", "0012020402150415020E00000000000000000000"),
    ("0000001001003A", "FF070000001001003A"), # Fixed length for chunks
    ("FE02170300000000000000000000000000000000", "0012020402130413020C00000000000000000000"),
    ("0000800000A5", "FF0500800000A5")
]

# =============================================================================
# HELPERS
# =============================================================================
class PacketReassembler:
    def __init__(self):
        self.buffer = bytearray()
        self.expected_chunks = 0
        self.total_len = 0
        self.in_progress = False

    def process_chunk(self, data):
        if not data: return []
        
        messages = []
        seq = data[0]

        if seq == 0xFE: # Header
            self.buffer = bytearray()
            self.total_len = data[2]
            # self.expected_chunks = data[3] # Approximate - not used
            self.in_progress = True
        elif seq == 0xFF: # EOF
            if self.in_progress:
                chunk_len = data[1]
                self.buffer.extend(data[2:2+chunk_len])
                # Validate length?
                messages.append(bytes(self.buffer))
                self.in_progress = False
        else: # Data Chunk
            if self.in_progress:
                chunk_len = data[1]
                self.buffer.extend(data[2:2+chunk_len])
        
        return messages

def create_control_command(type_id, value):
    if type_id == TYPE_SPEED:
         # 020402090409020101VVVV00CS
         base = bytearray.fromhex("020402090409020101")
         base.extend(struct.pack('<H', int(value)))
         base.extend(bytes([0x00])) # 1 byte padding matches control_speed.py
         # Checksum
         cs = sum(base[4:]) & 0xFF
         base.append(cs)
         return base
         
    elif type_id == TYPE_INCLINE:
         # 020402090409020102VVVV00CS
         base = bytearray.fromhex("020402090409020102")
         base.extend(struct.pack('<H', int(value))) # Value now matches Scale 100
         base.extend(bytes([0x00])) # 1 byte padding matches control_incline.py
         cs = sum(base[4:]) & 0xFF
         base.append(cs)
         return base
         
    return b''

class KeyPoller:
    def __enter__(self):
        self.fd = sys.stdin.fileno()
        self.old_settings = termios.tcgetattr(self.fd)
        tty.setcbreak(self.fd)
        return self

    def __exit__(self, type, value, traceback):
        termios.tcsetattr(self.fd, termios.TCSADRAIN, self.old_settings)

# =============================================================================
# CONTROLLER
# =============================================================================
class TreadmillController:
    def __init__(self):
        self.state = {
            "connected": False,
            "speed_actual": 0.0,
            "incline_actual": 0.0,
            "time_str": "00:00:00",
            "dist_mi": 0.0,
            "cals": 0.0,
        }
        self.target_speed_mph = None # None indicates not yet synced
        self.target_incline_pct = None
        self.pending_command = None
        self.reassembler = PacketReassembler()
        self.client = None
        self.loop = None

    def decode_telemetry(self, payload):
        if len(payload) < 30 or payload[3] != 0x2F: return

        try:
            # Speed (8)
            s_raw = struct.unpack_from('<H', payload, 8)[0]
            current_speed_mph = (s_raw / 100.0) * 0.621371
            
            # Sync Target on First Packet
            if self.target_speed_mph is None:
                self.target_speed_mph = round(current_speed_mph, 1)
            
            i_raw = struct.unpack_from('<H', payload, 10)[0]
            current_incline_pct = i_raw / 100.0
            
            if self.target_incline_pct is None:
                self.target_incline_pct = round(current_incline_pct, 1)
            
            self.state["speed_actual"] = current_speed_mph
            self.state["incline_actual"] = current_incline_pct
            
            # ... (Rest of decode unchanged) ...
            t_sec = struct.unpack_from('<I', payload, 27)[0]
            m, s = divmod(t_sec, 60)
            h, m = divmod(m, 60)
            self.state["time_str"] = f"{h:02}:{m:02}:{s:02}"
            
            if len(payload) >= 42 + 4:
                d_raw = struct.unpack_from('<I', payload, 42)[0]
                d_km = d_raw / 100.0 / 1000.0
                self.state["dist_mi"] = d_km * 0.621371
            
            c_raw = struct.unpack_from('<I', payload, 31)[0]
            self.state["cals"] = c_raw / 97656.0
            
            self.print_status()
                
        except Exception:
            pass

    def print_status(self):
        try:
            # Handle None targets for display
            tgt_s = self.target_speed_mph if self.target_speed_mph is not None else 0.0
            tgt_i = self.target_incline_pct if self.target_incline_pct is not None else 0.0
            
            s = (f"\r🏃 {self.state['speed_actual']:4.1f} MPH [Tgt: {tgt_s:4.1f}] | "
                 f"⛰️  {self.state['incline_actual']:4.1f}% [Tgt: {tgt_i:4.1f}] | "
                 f"⏱️  {self.state['time_str']} | 📏 {self.state['dist_mi']:6.3f} mi | 🔥 {self.state['cals']:4.1f} cal   ")
            sys.stdout.write(s)
            sys.stdout.flush()
        except Exception:
            pass

    def handle_stdin(self):
        try:
            key = sys.stdin.read(1)
            if not key: return
            self.process_key(key)
        except Exception:
            pass

    def process_key(self, key):
        if self.target_speed_mph is None: self.target_speed_mph = 0.0
        if self.target_incline_pct is None: self.target_incline_pct = 0.0
        
        cmd_updated = False
        k = key.lower()
        
        if k == 'q':
            print("\nExiting...")
            sys.exit(0)
            
        elif k == 's':
            if key == 'S': 
                # Increase logic
                if self.target_speed_mph < 1.0:
                    self.target_speed_mph = 1.0
                else:
                    self.target_speed_mph += 0.1
            else: 
                # Decrease logic
                # If we go below 1.0, stop (0.0)
                if self.target_speed_mph <= 1.0:
                    self.target_speed_mph = 0.0
                else: 
                    self.target_speed_mph -= 0.1
            cmd_updated = True
        
        elif k == 'i':
            if key == 'I': self.target_incline_pct += 0.5
            else: self.target_incline_pct -= 0.5
            cmd_updated = True
            
        if cmd_updated:
            # Clamp
            # Speed logic is partially handled above, but clamp max
            self.target_speed_mph = max(0.0, min(12.0, self.target_speed_mph))
            self.target_incline_pct = max(-3.0, min(15.0, self.target_incline_pct))
            
            # Queue Command
            if k == 's':
                kph = self.target_speed_mph * 1.60934
                val = int(kph * 100)
                self.pending_command = (TYPE_SPEED, val)
            else:
                val = int(self.target_incline_pct * 100) # Corrected Scale to 100
                self.pending_command = (TYPE_INCLINE, val)
            
            # Update UI immediately
            self.print_status()

    async def send_chunked_message_robust(self, payload, char_obj=None):
        # Logic from read_telemetry.py
        total_len = len(payload)
        chunks = []
        data_slices = [payload[i:i+18] for i in range(0, total_len, 18)]
        total_chunks = 1 + len(data_slices)
        
        # Header is FE 02 [Len] [Chunks] + Padding
        header = bytearray([0xFE, 0x02, total_len, total_chunks]) + b'\x00'*16
        chunks.append(header)
        
        for i, chunk in enumerate(data_slices):
            seq = 0xFF if i == len(data_slices)-1 else i
            chunks.append(bytearray([seq, len(chunk)]) + chunk)
            
        for pkt in chunks:
            target = char_obj if char_obj else UUID_TX
            await self.client.write_gatt_char(target, pkt)
            await asyncio.sleep(0.1) # SLOWER: 0.1 for stability

    async def connect_and_run(self):
        print(f"Scanning for {DEVICE_NAME}...")
        # Bleak 0.19.5+ Compatibility: find_device_by_name is deprecated/removed
        # Use discover and filter manually
        devices = await BleakScanner.discover()
        device = next((d for d in devices if d.name == DEVICE_NAME), None)
        if not device:
            print("Device not found.")
            return

        print(f"Connecting to {device.address}...")
        async with BleakClient(device) as client:
            self.client = client
            self.state["connected"] = True
            print("Connected. Performing Handshake (Robust Mode)...")
            
            # Ensure services are resolved (Fix for 'Characteristic not found' error)
            # if not client.services: await client.get_services() # Removed in Bleak 2.0
            
            # Resolve Write Char (Robust Fix)
            write_char = client.services.get_characteristic(UUID_TX)
            if not write_char:
                print(f"Critical: Could not find Write Char {UUID_TX}")
                return
            self.write_char = write_char

            await client.start_notify(UUID_RX, self.notification_handler)
            
            # Robust Handshake from read_telemetry.py
            CMD_1_INFO = bytes.fromhex("0204020402048187")
            CMD_2_CAPS = bytes.fromhex("0204020404048088")
            CMD_3_CMDS = bytes.fromhex("0204020404048890")
            CMD_4_INFO2 = bytes.fromhex("020402070207820000008B")
            CMD_5_INFO3 = bytes.fromhex("0204020602068400008C")
            CMD_6_95 = bytes.fromhex("020402040204959B")
            CMD_7_ENABLE = bytes.fromhex("0204022804289007018D68492815F0E9C0BDA89988756079704D484948757069609D88B9A8D5C0A0020000AD")
            CMD_8_UNK = bytes.fromhex("020402150415020E000000000000000000000000001001003A")
            CMD_9_START = bytes.fromhex("020402130413020C0000000000000000000000800000A5")
            
            init_cmds = [
                CMD_1_INFO, CMD_2_CAPS, CMD_3_CMDS, CMD_4_INFO2, 
                CMD_5_INFO3, CMD_6_95, CMD_7_ENABLE, CMD_8_UNK, CMD_9_START
            ]
            
            for cmd in init_cmds:
                await self.send_chunked_message_robust(cmd, write_char)
                if cmd == CMD_7_ENABLE or cmd == CMD_8_UNK:
                    await asyncio.sleep(0.5)
                elif cmd == CMD_9_START:
                    await asyncio.sleep(1.0)
                else:
                    await asyncio.sleep(0.1)
                
            print("\n========================================================")
            print(" iFit CLI Controller")
            print(" [S] Speed+0.1, [s] Speed-0.1")
            print(" [I] Inc+0.5,   [i] Inc-0.5")
            print(" [Q] Quit")
            print("========================================================")
            
            # Register stdin reader
            self.loop = asyncio.get_running_loop()
            try:
                self.loop.add_reader(sys.stdin, self.handle_stdin)
            except Exception as e:
                print(f"Warning: Could not add stdin reader ({e}). Input might not work.")
            
            # Valid connection established
            print("Starting Telemetry Loop...")
            
            # Loop
            while True:
                if self.pending_command:
                    await self.send_command_bytes(self.pending_command, self.write_char)
                    self.pending_command = None
                else:
                    # Poll
                    await self.send_chunked_message_robust(POLL_CMD, self.write_char)
                
                # Keep loop alive
                await asyncio.sleep(0.2)

    def notification_handler(self, sender, data):
        for msg in self.reassembler.process_chunk(data):
            self.decode_telemetry(msg)

    async def send_command_bytes(self, cmd_tuple, char_obj):
        type_id, val = cmd_tuple
        payload = create_control_command(type_id, val)
        await self.send_chunked_message_robust(payload, char_obj)

async def main():
    # Setup Terminal
    controller = TreadmillController()
    
    # Use context manager to restore terminal on exit
    with KeyPoller():
        try:
            await controller.connect_and_run()
        except Exception as e:
            print(f"\nRuntime Error: {e}")
            import traceback
            traceback.print_exc()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
    except Exception as e:
        print(f"Fatal Error: {e}")
    finally:
        # Ensure cursor is back/newlines work if crashed
        pass
