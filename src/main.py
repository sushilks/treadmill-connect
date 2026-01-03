#!/usr/bin/env python3
import asyncio
import logging
import sys
import struct
import time

# --- MONKEY PATCH FOR BLESS <-> BLEAK 0.22.x COMPATIBILITY ---
# Bless 0.2.6 relies on 'bleak.backends.corebluetooth.service' which was removed.
# We map it to the new location 'bleak.backends.service' or create a dummy.
try:
    import bleak.backends.corebluetooth.service
except ImportError:
    # Attempt to patch
    try:
        import bleak.backends.service
        # Create a dummy module for 'bleak.backends.corebluetooth.service'
        # pointing to 'bleak.backends.service' (where BleakGATTService likely resides now)
        # OR just mock it enough for Bless to import.
        # Actually, for CoreBluetooth, Bless expects BleakGATTServiceCoreBluetooth.
        # Let's hope mapping the module works.
        sys.modules["bleak.backends.corebluetooth.service"] = bleak.backends.service
    except Exception as e:
        print(f"Warning: Failed to patch bless/bleak: {e}")
# -------------------------------------------------------------

from typing import Any, Dict
from uuid import UUID

from bless import (
    BlessServer,
    BlessGATTCharacteristic,
    GATTCharacteristicProperties,
    GATTAttributePermissions
)
from bleak import BleakClient, BleakScanner

# =============================================================================
# LOGGING
# =============================================================================
# Configure Logging
logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s - %(message)s',
                    datefmt='%H:%M:%S')
logger = logging.getLogger(__name__)

# =============================================================================
# CONSOLE UI & LOGGING
# =============================================================================
class ConsoleUI:
    def __init__(self):
        self.status_line = ""
        self.is_tty = sys.stdout.isatty()
        self.last_print_time = 0
        
    def update_status(self, state):
        # Format: [Linked] Spd: 5.0 | Inc: 1.0 | Dist: 1.25 | Time: 20:30 | Cal: 150
        conn_str = "Linked" if state.connected_to_ifit else "Searching..."
        m, s = divmod(state.elapsed_time, 60)
        h, m = divmod(m, 60)
        time_str = f"{h:02d}:{m:02d}:{s:02d}"
        
        # Convert to Imperial
        spd_mph = state.speed_kph * 0.621371
        dist_mi = (state.distance_m / 1000.0) * 0.621371
        
        self.status_line = (
            f"[{conn_str}] "
            f"Spd: {spd_mph:.1f}mph | "
            f"Inc: {state.incline_pct:.1f}% | "
            f"Dist: {dist_mi:.2f}mi | "
            f"Time: {time_str} | "
            f"Cal: {state.calories}"
        )
        self.refresh()

    def refresh(self):
        if self.is_tty:
            # Interactive: Update line in place with ANSI codes
            sys.stdout.write(f"\r\x1b[K{self.status_line}")
            sys.stdout.flush()
        else:
            # Headless: Print cleanly (no control chars) and throttle (1s) to avoid log spam
            if time.time() - self.last_print_time > 1.0:
                 print(self.status_line, flush=True) 
                 self.last_print_time = time.time()
        
    def log(self, message):
        if self.is_tty:
            # Clear status line
            sys.stdout.write("\r\x1b[K")
            # Print message (scrolled)
            sys.stdout.write(f"{message}\n")
            # Reprint status line
            sys.stdout.write(self.status_line)
            sys.stdout.flush()
        else:
            print(message, flush=True)

ui = ConsoleUI()

class ScrollingLogHandler(logging.Handler):
    def emit(self, record):
        msg = self.format(record)
        ui.log(msg)

# Setup Logger
logger = logging.getLogger("IFIT-FTMS")
# Default to INFO (Hide Debug noise)
logger.setLevel(logging.INFO) 
handler = ScrollingLogHandler()
formatter = logging.Formatter('%(asctime)s - %(message)s', datefmt='%H:%M:%S')
handler.setFormatter(formatter)
logger.addHandler(handler)
# logging.basicConfig(level=logging.INFO)

# =============================================================================
# IFIT CLIENT CONSTANTS (From ifit-ctrl.py)
# =============================================================================
import os
IFIT_DEVICE_NAME = os.environ.get("IFIT_DEVICE_NAME", "I_TL")
UUID_TX = "00001534-1412-efde-1523-785feabcd123"
UUID_RX = "00001535-1412-efde-1523-785feabcd123"
POLL_CMD = bytes.fromhex("02040210041002000A13943300104010008018F2")

# Handshake Steps
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
    ("0000001001003A", "FF070000001001003A"),
    ("FE02170300000000000000000000000000000000", "0012020402130413020C00000000000000000000"),
    ("0000800000A5", "FF0500800000A5")
]

# =============================================================================
# FTMS SERVER CONSTANTS
# =============================================================================
FTMS_SERVICE_UUID = "00001826-0000-1000-8000-00805F9B34FB"
FTMS_DATA_CHAR_UUID = "00002ACD-0000-1000-8000-00805F9B34FB"
FTMS_CONTROL_POINT_UUID = "00002AD9-0000-1000-8000-00805F9B34FB"
FTMS_FEATURE_UUID = "00002ACC-0000-1000-8000-00805F9B34FB"
FTMS_STATUS_UUID = "00002ADA-0000-1000-8000-00805F9B34FB"
FTMS_TRAINING_STATUS_UUID = "00002AD3-0000-1000-8000-00805F9B34FB"
FTMS_SPEED_RANGE_UUID = "00002AD4-0000-1000-8000-00805F9B34FB"
FTMS_INCLINE_RANGE_UUID = "00002AD5-0000-1000-8000-00805F9B34FB"

SERVER_NAME = "mytm"

# Feature Mask: 
# Byte 0: Bits 0-7. 0x22 = (Bit 1: Total Dist, Bit 5: Inclination)
# Byte 1: Bits 8-15. 0x01 = (Bit 8: Expended Energy)
# Byte 4: Bits 0-7 (Target). 0x03 = (Bit 0: Speed Target, Bit 1: Inc Target)
FTMS_FEATURE_VAL = b'\x22\x01\x00\x00\x03\x00\x00\x00'

# =============================================================================
# SHARED STATE
# =============================================================================
class BridgeState:
    def __init__(self):
        self.connected_to_ifit = False
        self.speed_kph = 0.0
        self.incline_pct = 0.0
        self.distance_m = 0 # Cumulative
        self.elapsed_time = 0 
        self.calories = 0
        self.ftms_client_connected = False
        self.ftms_last_activity_time = time.time()  # Initialize to now, not 0
        self.pause_hci_monitor = False  # Pause hcitool while scanning/connecting to iFit
        self.control_queue = asyncio.Queue()
        self.last_notify_time = time.time()
        self.response_queue = asyncio.Queue()
        self.last_ftms_payload = None
        self.last_ftms_payload = None
        self.last_update_ts = 0
        self.initial_t_raw = None
        self.initial_cal_raw = None

state = BridgeState()

# =============================================================================
# CONSTANTS & PROTOCOL
# =============================================================================
TYPE_SPEED = 0x01
TYPE_INCLINE = 0x02

# ... (Previous imports and Constants) ...

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================
def create_control_command(type_id, value):
    if type_id == TYPE_SPEED:
         # 020402090409020101VVVV00CS
         base = bytearray.fromhex("020402090409020101")
         base.extend(struct.pack('<H', int(value)))
         base.extend(bytes([0x00]))
         cs = sum(base[4:]) & 0xFF
         base.append(cs)
         return base
    elif type_id == TYPE_INCLINE:
         # 020402090409020102VVVV00CS
         base = bytearray.fromhex("020402090409020102")
         base.extend(struct.pack('<H', int(value))) # Scale 100
         base.extend(bytes([0x00]))
         cs = sum(base[4:]) & 0xFF
         base.append(cs)
         return base
    return b''

async def send_chunked_robust(client, payload, char_obj=None):
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
        target = char_obj if char_obj else UUID_TX
        await client.write_gatt_char(target, pkt)
        await asyncio.sleep(0.1) # SLOWER: 0.02 -> 0.1 for stability

async def robust_handshake(client, write_char):
    logger.info("Performing Robust Handshake...")
    # Using init commands from read_telemetry.py logic
    CMD_1 = bytes.fromhex("0204020402048187")
    CMD_2 = bytes.fromhex("0204020404048088")
    CMD_3 = bytes.fromhex("0204020404048890")
    CMD_4 = bytes.fromhex("020402070207820000008B")
    CMD_5 = bytes.fromhex("0204020602068400008C")
    CMD_6 = bytes.fromhex("020402040204959B")
    CMD_7 = bytes.fromhex("0204022804289007018D68492815F0E9C0BDA89988756079704D484948757069609D88B9A8D5C0A0020000AD")
    CMD_8 = bytes.fromhex("020402150415020E000000000000000000000000001001003A")
    CMD_9 = bytes.fromhex("020402130413020C0000000000000000000000800000A5")
    
    cmds = [CMD_1, CMD_2, CMD_3, CMD_4, CMD_5, CMD_6, CMD_7, CMD_8, CMD_9]
    
    for cmd in cmds:
        await send_chunked_robust(client, cmd, write_char)
        if cmd == CMD_7 or cmd == CMD_8:
            await asyncio.sleep(0.5)
        elif cmd == CMD_9:
            await asyncio.sleep(1.0)
        else:
            await asyncio.sleep(0.1)

class PacketReassembler:
    def __init__(self):
        self.buffer = bytearray()
        self.in_progress = False

    def process_chunk(self, data):
        if not data: return []
        messages = []
        seq = data[0]
        if seq == 0xFE:
            self.buffer = bytearray()
            self.in_progress = True
        elif seq == 0xFF:
            if self.in_progress:
                chunk_len = data[1]
                self.buffer.extend(data[2:2+chunk_len])
                messages.append(bytes(self.buffer))
                self.in_progress = False
        else:
            if self.in_progress:
                chunk_len = data[1]
                self.buffer.extend(data[2:2+chunk_len])
        return messages

async def ifit_client_loop(server: BlessServer):
    reassembler = PacketReassembler()
    
    def decode_telemetry(sender, data):
        # ... (Existing Decode Logic remains same, omitted for brevity, logic is inside inner loop)
        # We need to define this function or keep the existing one.
        # Ideally we only modify the LOOP structure around it.
        pass

    # Re-define decode locally or assume we wrap the existing logic? 
    # Since I'm using multi-replace, I should replace the LOOP logic primarily.
    # But decode_telemetry is inside the function scope.
    # Let me preserve decode_telemetry by NOT replacing it, but I cannot modify the loop *around* it easily without touching it.
    # ACTUALLY: decode_telemetry is huge. I should try to leave it alone.
    # I will replace the START of the function and the WHILE loop, but I have to be careful about indentation.
    
    # Let's extract decode_telemetry logic if possible? No, it uses local variables like reassembler.
    
    # BETTER APPROACH: Modify the 'while True' loop and the connection logic, keeping the inner helper intact if possible.
    # But the inner helper is defined *inside* the function.
    
    # Strategy: Replace the entire function `ifit_client_loop`.
    # I will copy the decode_telemetry from the source file content I have.
        try:
             # Feed Watchdog
             state.last_notify_time = time.time()
             
             for payload in reassembler.process_chunk(data):
                if len(payload) < 30 or payload[3] != 0x2F: continue
                
                # Divide by 100 for units
                s_raw = struct.unpack_from('<H', payload, 8)[0]
                i_raw = struct.unpack_from('<H', payload, 10)[0]
                
                # Update State
                state.speed_kph = s_raw / 100.0
                state.incline_pct = i_raw / 100.0
                
                # Distance Strategy: Prefer Treadmill, Fallback to Calculation
                if len(payload) >= 46:
                    d_raw = struct.unpack_from('<I', payload, 42)[0]
                    dist_val = d_raw / 100.0
                    
                    if dist_val > 0:
                        state.distance_m = dist_val
                    else:
                        current_time = time.time()
                        if hasattr(state, 'last_calc_time'):
                            dt = current_time - state.last_calc_time
                            if dt > 0 and dt < 2.0: 
                                m_per_s = (state.speed_kph * 1000) / 3600.0
                                state.distance_m += (m_per_s * dt)
                        state.last_calc_time = current_time
                
                # Time (Offset 27)
                if len(payload) >= 31:
                     t_raw = struct.unpack_from('<I', payload, 27)[0]
                     if state.initial_t_raw is None: state.initial_t_raw = t_raw
                     if t_raw < state.initial_t_raw: state.initial_t_raw = t_raw # Handle wrap/reset
                     state.elapsed_time = t_raw - state.initial_t_raw
                    
                # Calories
                if len(payload) >= 35:
                     cal_raw = struct.unpack_from('<I', payload, 31)[0]
                     if state.initial_cal_raw is None: state.initial_cal_raw = cal_raw
                     if cal_raw < state.initial_cal_raw: state.initial_cal_raw = cal_raw
                     state.calories = int((cal_raw - state.initial_cal_raw) / 97656.0) 
                
                # Notify FTMS (Fire and Forget)
                asyncio.create_task(update_ftms(server))
                
                # Update Console UI
                ui.update_status(state)
        except Exception as e:
             logger.error(f"Decode Error: {e}")

    logger.info("Starting iFit Client Loop (Lazy Mode - Handoff Strategy)...")
    while True:
        try:
            # 0. LAZY WAIT: Only proceed if FTMS Client is connected (or Handoff Signaled)
            if not state.ftms_client_connected and PI_MODE:
                # Wait for client...
                await asyncio.sleep(1.0)
                continue
            
            # 1. Discovery - PAUSE HCI MONITOR TO AVOID BLUEZ CONTENTION
            state.pause_hci_monitor = True
            
            # SILENCE PHASE: Stop Advertising so phone cannot reconnect while we are busy
            if PI_MODE:
                try:
                    logger.info("🤫 Stopping Advertising (Silence Phase via hciconfig)...")
                    # bless doesn't expose stop_advertising for BlueZ, use system tool
                    import subprocess
                    subprocess.run(["sudo", "hciconfig", "hci0", "noleadv"], check=False)
                    await asyncio.sleep(0.5) 
                except Exception as adv_e:
                    logger.warning(f"Failed to stop advertising: {adv_e}")
            
            # Use Standard Scanning (Handoff Strategy clears the air for this)
            if PI_MODE:
                 devices_map = await BleakScanner.discover(return_adv=True)
                 target_entry = next((e for e in devices_map.values() if e[0].name == IFIT_DEVICE_NAME), None)
                 device = target_entry[0] if target_entry else None
                 rssi = target_entry[1].rssi if target_entry else 0
            else:
                 devices = await BleakScanner.discover()
                 device = next((d for d in devices if d.name == IFIT_DEVICE_NAME), None)
                 rssi = getattr(device, 'rssi', 0) if device else 0
            
            if device:
                # Get address - device is BLEDevice object
                device_address = device.address
                device_name = device.name
                
                if PI_MODE:
                    logger.info(f"Found iFit Device: {device_name} (RSSI: {rssi})")
                    if rssi < -80 and rssi != 0:
                         logger.warning(f"⚠️ Weak Signal ({rssi} dBm). Move Pi closer to Treadmill!")
                
                # PRE-EMPTIVE ZOMBIE KILLER (Targeted)
                if PI_MODE:
                     try:
                         import subprocess
                         # Check if we are already 'physically' connected to the treadmill (Zombie)
                         proc = subprocess.run(["sudo", "hcitool", "con"], capture_output=True, text=True)
                         if device_address in proc.stdout:
                             # Parse handle. fmt: "> LE 61:36:1D:64:12:F3 handle 2 state 1 lm SLAVE"
                             # We look for the line with our MAC
                             for line in proc.stdout.split('\n'):
                                 if device_address in line and "handle" in line:
                                     parts = line.split()
                                     try:
                                         idx = parts.index('handle')
                                         handle = parts[idx+1]
                                         logger.warning(f"🧟 Zombie Detected ({device_address} hdl={handle}). Surgically removing...")
                                         # Fix Race: Stop Adv BEFORE disconnecting
                                         subprocess.run(["sudo", "hciconfig", "hci0", "noleadv"], check=False)
                                         subprocess.run(["sudo", "hcitool", "ledc", handle], check=False)
                                         await asyncio.sleep(1.5) # Wait for controller to update
                                     except: pass
                     except Exception as e:
                         logger.error(f"Pre-emptive Kill Error: {e}")

                # INNER RETRY LOOP - Skip rescan on timeout, retry with same device
                for attempt in range(3):
                    try:
                        # FAIL FAST: 10s timeout
                        async with BleakClient(device, timeout=10.0, disconnected_callback=lambda c: logger.warning("⚠️ iFit Link Lost (Callback)")) as client:
                            state.connected_to_ifit = True
                            state.initial_t_raw = None
                            state.initial_cal_raw = None
                            logger.info(f"Connected to iFit Treadmill (Attempt {attempt+1})")
                            
                            write_char = client.services.get_characteristic(UUID_TX)
                            if not write_char:
                                logger.error(f"Could not find Write Char {UUID_TX}")
                                state.connected_to_ifit = False
                                break  # Break inner loop, rescan

                            await client.start_notify(UUID_RX, decode_telemetry)
                            await robust_handshake(client, write_char)
                            logger.info("Handshake Complete. Loop Active.")
                            
                            # RESUME HCI MONITOR - Connection established
                            state.pause_hci_monitor = False
                            
                            # WAKE UP PHASE: Restart Advertising so phone can reconnect
                            if PI_MODE:
                                logger.info("⏳ Stabilizing Link (Wait 3s)...")
                                await asyncio.sleep(3.0) 
                                try:
                                    logger.info("📢 Restarting Advertising (hciconfig leadv 0)...")
                                    import subprocess
                                    subprocess.run(["sudo", "hciconfig", "hci0", "leadv", "0"], check=False)
                                except Exception as adv_e:
                                    logger.warning(f"Failed to start advertising: {adv_e}")
                            
                            # Connection Loop
                            while client.is_connected:
                                current_time = time.time()
                                
                                # --- DISCONNECT CHECK ---
                                if not state.ftms_client_connected and PI_MODE:
                                    idle_time = current_time - state.ftms_last_activity_time
                                    if idle_time > 60.0:
                                        logger.info(f"💤 Idle for {idle_time:.1f}s. Disconnecting from iFit to save power.")
                                        break
                                
                                # 1. Process Queue
                                command_count = 0
                                command_sent = False
                                while not state.control_queue.empty() and command_count < 5:
                                    cmd_type, val = await state.control_queue.get()
                                    pkt = create_control_command(cmd_type, val)
                                    if pkt:
                                        logger.debug(f"Sending Command: Type={cmd_type} Val={val}")
                                        try:
                                            await send_chunked_robust(client, pkt, write_char)
                                            command_sent = True
                                            command_count += 1
                                            await asyncio.sleep(0.1)
                                        except Exception as e:
                                            logger.error(f"Command Send Error: {e}")
                                            break 
                                if not client.is_connected: break

                                # 2. Poll
                                if not command_sent or (current_time - state.last_notify_time > 1.0):
                                     try:
                                        await send_chunked_robust(client, POLL_CMD, write_char)
                                     except Exception as e:
                                        logger.error(f"Poll Write Error: {e}")
                                        break
                                
                                # 3. Watchdog Check
                                if time.time() - state.last_notify_time > 5.0:
                                     logger.warning("Watchdog: Telemetry Stalled > 5s. Reconnecting...")
                                     break
                                     
                                await asyncio.sleep(0.2) 
                                
                            state.connected_to_ifit = False
                            logger.info("Client Disconnected (Loop Ended)")
                            break  # Success - break inner retry loop
                            
                    except asyncio.TimeoutError:
                        # Timeout! Clear ghost and retry IMMEDIATELY (no rescan)
                        logger.warning(f"⏱️ Timeout on Attempt {attempt+1}/3. Clearing line...")
                        try:
                            import subprocess
                            subprocess.run(["bluetoothctl", "disconnect", device_address], check=False, timeout=2.0)
                        except: pass
                        await asyncio.sleep(0.5)  # Brief pause
                        continue  # Retry with same device object
                        
                    except Exception as e:
                        logger.error(f"Connect Error (Att {attempt+1}): {repr(e)}")
                        break  # Other errors - break and rescan
                        
            else:
                logger.debug("iFit Device not found, retrying...")
                await asyncio.sleep(2.0) 
                
        except Exception as e:
            import traceback
            logger.error(f"iFit Client Error: {repr(e)}")
            logger.debug(traceback.format_exc())
            state.connected_to_ifit = False
            state.pause_hci_monitor = False  # RESUME HCI MONITOR on error
            
            # RECOVERY: Restart Advertising so we don't stay silent
            if PI_MODE:
                try:
                    import subprocess
                    subprocess.run(["sudo", "hciconfig", "hci0", "leadv", "0"], check=False)
                except: pass
            
            # Zombie Killer: If we timed out, BlueZ might think we are connected. Force disconnect.
            if "TimeoutError" in repr(e) and 'device_address' in locals() and device_address:
                try:
                    logger.warning(f"Timeout detected. Attempting to clear ghost connection to {device_address}...")
                    import subprocess
                    subprocess.run(["bluetoothctl", "disconnect", device_address], check=False, timeout=5.0)
                except Exception as z:
                    logger.error(f"Zombie Killer Failed: {z}")
            
            # Fast retry for TimeoutError, slower for other errors
            if "TimeoutError" in repr(e):
                await asyncio.sleep(1.0)  # Quick retry after cleanup
            else:
                await asyncio.sleep(5.0)

async def update_ftms(server: BlessServer):
    if not server or not state.connected_to_ifit: return
    
    # Flags: Speed (0) is implicit?, Dist (2), Inc (3)
    # 0x01: Inst. Speed? No, usually mandatory.
    # Standard Flags for Treadmill Data 2ACD:
    # Bit 0: More Data
    # Bit 1: Avg Speed
    # Bit 2: Total Distance present
    # Bit 3: Inclination & Ramp Angle present
    # Bit 4: Elevation Gain present
    # ...
    # We want Dist(2) + Inc(3) = 0x04 + 0x08 = 0x0C (12)
    # Plus Elapsed Time(10) = 0x0400
    # Plus Expended Energy(7) = 0x0080
    # Total = 0x048C
    flags = 0x048C
    # ... (rest of function same) ...
    
    # Speed: uint16, 0.01 km/h
    speed_val = int(state.speed_kph * 100)
    
    # Distance: uint24, 1 meter
    dist_val = int(state.distance_m)
    
    # Incline: sint16, 0.1 %
    inc_val = int(state.incline_pct * 10)
    
    # Pack: Flags(H), Speed(H), Dist(3 bytes), Incline(h)
    # Dist is tricky (3 bytes). Pack into I and take 3?
    payload = bytearray()
    payload.extend(struct.pack('<H', flags))
    payload.extend(struct.pack('<H', speed_val))
    
    d_bytes = struct.pack('<I', dist_val)[:3]
    payload.extend(d_bytes)
    
    payload.extend(struct.pack('<h', inc_val))
    # CRITICAL FIX: If Flag Bit 3 (Inc) is set, Spec requires Ramp Angle (2 bytes) too!
    # Send 0 instead of 0x7FFF for cleaner UI
    payload.extend(struct.pack('<h', 0))
    
    # Flags Bit 7: Expended Energy (Total(2), PerHour(2), PerMin(1))
    # MUST be before Elapsed Time (Bit 10)
    if (flags & 0x0080):
         # Total (Calories)
         c_val = min(state.calories, 65535)
         payload.extend(struct.pack('<H', c_val))
         # Per Hour (Not Available)
         payload.extend(struct.pack('<H', 0xFFFF))
         # Per Min (Not Available)
         payload.extend(struct.pack('<B', 0xFF))

    # Flags Bit 10: Elapsed Time (2 bytes, seconds)
    if (flags & 0x0400):
        # Limit to 65535 (uint16 max)
        t_val = min(state.elapsed_time, 65535)
        payload.extend(struct.pack('<H', t_val))
    
    # Smart Update: Only notify if changed OR > 5 seconds passed
    import time
    now = time.time()
    if payload == state.last_ftms_payload and (now - state.last_update_ts) < 5.0:
         return # Skip update to save bandwidth
        
    state.last_ftms_payload = payload
    state.last_update_ts = now
    
    logger.debug(f"FTMS NOTIFY: {payload.hex()}")
    
    # Notify
    try:
        server.get_characteristic(FTMS_DATA_CHAR_UUID).value = bytes(payload)
        server.update_value(FTMS_SERVICE_UUID, FTMS_DATA_CHAR_UUID)
    except Exception as e:
        logger.debug(f"FTMS Update Error: {e}")

# Global Server Reference
ftms_server = None

def handle_read(characteristic: BlessGATTCharacteristic, **kwargs):
    logger.info(f"FTMS READ: {characteristic.uuid}")
    return characteristic.value

def handle_control_point(characteristic: BlessGATTCharacteristic, value: bytearray, **kwargs):
    global ftms_server
    # OpCodes
    # 0x00: Request Control
    # 0x01: Reset
    # 0x07: Start/Resume
    # 0x08: Stop/Pause
    # 0x11: Set Target Speed (uint16 0.01km/h)
    # 0x12: Set Target Incline (sint16 0.1%)
    
    # Response OpCode is 0x80
    # Structure: [0x80, RequestOpCode, ResultCode]
    # ResultCode: 0x01 (Success), 0x02 (OpCode not supported), 0x03 (Invalid Param)
    
    if len(value) < 1: return
    opcode = value[0]
    
    logger.debug(f"FTMS Control: Opcode={opcode} Val={value.hex()}")
    
    # Get Server from kwargs or global
    server_obj = kwargs.get('server') or ftms_server
    # Bless callback signature doesn't pass server easily. We might need to access global 'server' ref if possible
    # or rely on bless updating the characteristic value if we return it? 
    # Bless documentation says write_request_func should indicate if needed.
    
    # Actually, for Indications, we need to update the value and trigger notify/indicate.
    # Let's assume we can't easily get the 'server' object here without a global or closure.
    # To keep it simple, let's try to just accept the command for now, ensuring we handle 0x00.
    
    response = bytearray([0x80, opcode, 0x01]) # Default Success
    
    if opcode == 0x00: # Request Control
        logger.info("Control Requested -> Granting")
        if server_obj:
            # Send correct status based on current speed
            if state.speed_kph > 0:
                # 0x04: Started / Resumed
                logger.info("Status Update: Started (0x04)")
                val = b'\x04'
            else:
                # 0x02: Stopped (Param 0x01 = Stop)
                logger.info("Status Update: Stopped (0x02)")
                val = b'\x02\x01'
            
            try:
                server_obj.get_characteristic(FTMS_STATUS_UUID).value = val
                server_obj.update_value(FTMS_SERVICE_UUID, FTMS_STATUS_UUID)
            except Exception as e:
                logger.error(f"Status Update Error: {e}")
        
    elif opcode == 0x07: # Start
         logger.info("🎮 FTMS Start / Resume")
         # Send Status: Started (0x04)
         if server_obj:
             try:
                 server_obj.get_characteristic(FTMS_STATUS_UUID).value = b'\x04'
                 server_obj.update_value(FTMS_SERVICE_UUID, FTMS_STATUS_UUID)
             except Exception: pass
         pass
         
    elif opcode == 0x08: # Stop
         logger.info("🎮 FTMS Stop")
         state.control_queue.put_nowait((TYPE_SPEED, 0))
         # Send Status: Stopped (0x02) + Stop (0x01)
         if server_obj:
             try:
                 server_obj.get_characteristic(FTMS_STATUS_UUID).value = b'\x02\x01'
                 server_obj.update_value(FTMS_SERVICE_UUID, FTMS_STATUS_UUID)
             except Exception: pass
         
    elif opcode == 0x02 and len(value) >= 3: # Set Target Speed (MANDATORY)
         val_raw = struct.unpack_from('<H', value, 1)[0]
         # FTMS: 0.01 km/h resolution (e.g. 500 = 5.0 km/h)
         # iFit: 0.01 km/h resolution (e.g. 500 = 5.0 km/h) [Based on telemetry decode]
         ifit_val = int(val_raw)
         kph = val_raw/100.0
         mph = kph * 0.621371
         logger.info(f"🎮 Set Speed: {kph} km/h ({mph:.1f} mph)")
         state.control_queue.put_nowait((TYPE_SPEED, ifit_val))
         # Send Status: Target Speed Changed (0x05) + Speed
         if server_obj:
             try:
                 val = b'\x05' + struct.pack('<H', val_raw)
                 server_obj.get_characteristic(FTMS_STATUS_UUID).value = val
                 server_obj.update_value(FTMS_SERVICE_UUID, FTMS_STATUS_UUID)
             except Exception: pass
         
    elif opcode == 0x03 and len(value) >= 3: # Set Target Inclination (MANDATORY)
         val_raw = struct.unpack_from('<h', value, 1)[0]
         # FTMS: 0.1% resolution (e.g. 100 = 10.0%)
         # iFit: 0.01% resolution? Telemetry is /100.0. 
         # So iFit 10.0% = 1000.
         # We need to multiply FTMS(100) by 10 to get iFit(1000).
         ifit_val = int(val_raw * 10) 
         logger.info(f"🎮 Set Incline: {val_raw/10.0}%")
         state.control_queue.put_nowait((TYPE_INCLINE, ifit_val))
         # Send Status: Target Incline Changed (0x06) + Incline
         if server_obj:
             try:
                 val = b'\x06' + struct.pack('<h', val_raw)
                 server_obj.get_characteristic(FTMS_STATUS_UUID).value = val
                 server_obj.update_value(FTMS_SERVICE_UUID, FTMS_STATUS_UUID)
             except Exception: pass
         
    # To Send Indication in Bless, we need "server.update_value". But we don't have 'server' here.
    # We will use a queue to send response back to main loop to send indication.
    state.response_queue.put_nowait(response)
    
    return value # Explicit return to ACK the write? Check bless docs/source.
                 # Bless code: `res = write_request_func(...)` -> `await self.write_gatt_char(..., res)` ?
                 # Actually bless updates value internally if we return it. If we return None, it might not update.
                 # But we want to update it. So return value.


# =============================================================================
# FTMS SERVER LOGIC
# =============================================================================
def handle_read(characteristic: BlessGATTCharacteristic, **kwargs):
    logger.info(f"FTMS READ: {characteristic.uuid} ({characteristic.description})")
    
    # Training Status (Read+Notify) - 2AD3
    if characteristic.uuid == FTMS_TRAINING_STATUS_UUID:
        return b'\x00\x01' # Flags=0, Status=1 (Idle)

    # Supported Speed Range - 2AD4
    if characteristic.uuid == FTMS_SPEED_RANGE_UUID:
        # Min(1.0), Max(20.0), Inc(0.1)
        return struct.pack('<HHH', 100, 2000, 10)
        
    # Supported Incline Range - 2AD5
    if characteristic.uuid == FTMS_INCLINE_RANGE_UUID:
        # Min(-6.0), Max(15.0), Inc(0.1)
        return struct.pack('<hhh', -60, 150, 10)
        
    # Feature (Read) - 2ACC
    if characteristic.uuid == FTMS_FEATURE_UUID:
        return FTMS_FEATURE_VAL

    return characteristic.value

async def ftms_server_loop():
    global ftms_server
    logger.info("Starting FTMS Server...")
    
    server = BlessServer(name=SERVER_NAME)
    ftms_server = server # Expose globally
    
    server.read_request_func = handle_read
    server.write_request_func = handle_control_point
    
    # Add FTMS Service
    await server.add_new_service(FTMS_SERVICE_UUID)
    
    # Add Characteristics
    # Treadmill Data (Notify)
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_DATA_CHAR_UUID,
        GATTCharacteristicProperties.notify,
        None,
        GATTAttributePermissions.readable
    )
    
    # Control Point (Write + Indicate)
    # Control Point (Write + Indicate)
    # FTMS Spec requires Indicate. Some apps check this property strictcly.
    # Enabling Indicate with Pairable=OFF works on BlueZ if permissions allow.
    cp_props = GATTCharacteristicProperties.write | GATTCharacteristicProperties.indicate
    
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_CONTROL_POINT_UUID,
        cp_props,
        None,
        GATTAttributePermissions.writeable
    )


    
    # Feature (Read)
    # Features (2ACC)
    # Use None to force dynamic read (for logging)
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_FEATURE_UUID,
        GATTCharacteristicProperties.read,
        None,
        GATTAttributePermissions.readable
    )
    
    # Device Information Service (180A)
    # Required by many apps
    DIS_SERVICE_UUID = "0000180A-0000-1000-8000-00805F9B34FB"
    await server.add_new_service(DIS_SERVICE_UUID)
    
    # Manufacturer Name (2A29)
    await server.add_new_characteristic(
        DIS_SERVICE_UUID,
        "00002A29-0000-1000-8000-00805F9B34FB",
        GATTCharacteristicProperties.read,
        b"iFit Bridge",
        GATTAttributePermissions.readable
    )
    
    # Model Number (2A24)
    await server.add_new_characteristic(
        DIS_SERVICE_UUID,
        "00002A24-0000-1000-8000-00805F9B34FB",
        GATTCharacteristicProperties.read,
        b"Loma-1",
        GATTAttributePermissions.readable
    )

    # Firmware Revision String (2A26)
    await server.add_new_characteristic(
        DIS_SERVICE_UUID,
        "00002A26-0000-1000-8000-00805F9B34FB",
        GATTCharacteristicProperties.read,
        b"1.0.0",
        GATTAttributePermissions.readable
    )

    # Serial Number String (2A25)
    await server.add_new_characteristic(
        DIS_SERVICE_UUID,
        "00002A25-0000-1000-8000-00805F9B34FB",
        GATTCharacteristicProperties.read,
        b"123456789",
        GATTAttributePermissions.readable
    )

    # Fitness Machine Status (2ADA)
    # Required for status updates (Reset, Stopped, etc)
    FTMS_STATUS_UUID = "00002ADA-0000-1000-8000-00805F9B34FB"
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_STATUS_UUID,
        GATTCharacteristicProperties.notify,
        None,
        GATTAttributePermissions.readable
    )

    # Training Status (2AD3) - Optional but good to have
    # Flags(0) + Status(1=Idle)
    # Must use None for value if Notify is set (CoreBluetooth requirement)
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_TRAINING_STATUS_UUID,
        GATTCharacteristicProperties.read | GATTCharacteristicProperties.notify,
        None,
        GATTAttributePermissions.readable
    )

    # Supported Speed Range (2AD4)
    # Use None value to force dynamic read (so we can log it)
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_SPEED_RANGE_UUID,
        GATTCharacteristicProperties.read,
        None,
        GATTAttributePermissions.readable
    )

    # Supported Inclination Range (2AD5)
    # Use None value to force dynamic read
    await server.add_new_characteristic(
        FTMS_SERVICE_UUID,
        FTMS_INCLINE_RANGE_UUID,
        GATTCharacteristicProperties.read,
        None,
        GATTAttributePermissions.readable
    )

    # GAP Appearance (Treadmill = 1348 = 0x0544)
    # Pi Mode: BlueZ manages GAP. Do NOT add it manually or it crashes.
    if not PI_MODE:
        GAP_SERVICE_UUID = "00001800-0000-1000-8000-00805F9B34FB"
        # Try adding to existing service if possible, or new one
        try:
            await server.add_new_service(GAP_SERVICE_UUID)
        except Exception:
            pass # Service might exist
            
        await server.add_new_characteristic(
            GAP_SERVICE_UUID,
            "00002A01-0000-1000-8000-00805F9B34FB",
            GATTCharacteristicProperties.read,
            struct.pack('<H', 0x0544),
            GATTAttributePermissions.readable
        )

    logger.info(f"Advertising as {SERVER_NAME}...")
    
    # --- PI MODE FIXES ---
    if PI_MODE:
        try:
            from bless.backends.bluezdbus.application import BlueZGattApplication
            # Monkey Patch: BlueZ owns GAP (1800). Prevent Bless from trying to claim it.
            original_add_service = BlueZGattApplication.add_service
            def patched_add_service(self, service):
                if str(service.uuid).upper() == "00001800-0000-1000-8000-00805F9B34FB":
                    logger.info("Ghost Patch: Skipping GAP Service Registration (BlueZ Owned)")
                    return
                return original_add_service(self, service)
            BlueZGattApplication.add_service = patched_add_service
        except ImportError:
            # Expected on Mac/Windows
            logger.debug("Could not apply BlueZ Monkey Patch (Backend not found?)")
            
    # Update Model Number for clarity
    if PI_MODE: 
        pass 

    # Explicitly advertise ONLY FTMS to avoid packet overflow
    # DIS and GAP are still discoverable after connection
    await server.start(advertising_service_uuids=[FTMS_SERVICE_UUID])
    
    # --- PI MODE SECURITY ENFORCEMENT ---
    # Bless/BlueZ often resets 'pairable' to on during Start. We must force it OFF now.
    if PI_MODE:
        import subprocess
        try:
            logger.info("Enforcing LE Security: Pairable=OFF via bluetoothctl")
            subprocess.run(["bluetoothctl", "pairable", "off"], check=False)
            # Ensure visibility is still ON
            subprocess.run(["bluetoothctl", "discoverable", "on"], check=False)
        except Exception as e:
            logger.error(f"Failed to enforce security: {e}")
    # ------------------------------------
    
    # Start Client Loop in background
    if "--mock" in sys.argv:
        asyncio.create_task(mock_client_loop(server))
    else:
        asyncio.create_task(ifit_client_loop(server))
    
    # Server Keepalive & Response Processor
    # Start Telemetry Loop
    asyncio.create_task(ftms_telemetry_loop(server))
    
    # Start Security Watchdog
    asyncio.create_task(security_watchdog_loop())
    
    # Start Connection Monitor
    asyncio.create_task(monitor_ftms_connection_loop())
    
    while True:
        # 1. Process Indications (FAST)
        while not state.response_queue.empty():
            response = await state.response_queue.get()
            logger.debug(f"Sending Indication: {response.hex()}")
            try:
                server.get_characteristic(FTMS_CONTROL_POINT_UUID).value = bytes(response)
                success = server.update_value(FTMS_SERVICE_UUID, FTMS_CONTROL_POINT_UUID)
                if not success:
                    logger.warning("Indication update_value returned False")
            except Exception as e:
                logger.error(f"Indication Error: {e}")
        
        await asyncio.sleep(0.05) # Low latency for control responses

# Global Scope Telemetry Loop
async def ftms_telemetry_loop(server: BlessServer):
    while True:
        if state.connected_to_ifit:
            await update_ftms(server)
        await asyncio.sleep(0.5) # Check often, update_ftms filters dupes

# Security Watchdog (Persistent Enforcement)
async def security_watchdog_loop():
    if not PI_MODE:
        return
        
    logger.info("Starting Security Watchdog (Interval: 10s)...")
    import subprocess
    
    while True:
        try:
            # Check current state
            result = subprocess.run(["bluetoothctl", "show"], capture_output=True, text=True)
            if "Pairable: yes" in result.stdout:
                logger.warning("Watchdog: Pairable is ON. Forcing OFF.")
                subprocess.run(["bluetoothctl", "pairable", "off"], check=False)
                subprocess.run(["bluetoothctl", "discoverable", "on"], check=False)
        except Exception as e:
            logger.error(f"Watchdog Error: {e}")
            
        await asyncio.sleep(10.0)

# Connection Monitor (Lazy Connect Support)
async def monitor_ftms_connection_loop():
    if not PI_MODE:
        return
        
    logger.info("Starting Connection Monitor (Interval: 3s)...")
    import subprocess
    
    while True:
        try:
            # PAUSE CHECK: Skip hcitool if iFit loop is scanning/connecting
            if state.pause_hci_monitor:
                await asyncio.sleep(1.0)
                continue
                
            # Check hcitool con for SLAVE connections (Incoming from Phone)
            # Output: "> LE 61:36:1D:64:12:F3 handle 2 state 1 lm SLAVE" 
            # OR: "> LE ... lm PERIPHERAL" (Newer BlueZ)
            result = subprocess.run(["sudo", "hcitool", "con"], capture_output=True, text=True)
            
            # Check for either SLAVE or PERIPHERAL
            has_client = "SLAVE" in result.stdout or "PERIPHERAL" in result.stdout

            if has_client:
                 if not state.ftms_client_connected:
                     # FIRST CONNECTION DETECTED!
                     
                     # HANDOFF STRATEGY: 
                     # If iFit is NOT connected, we must disconnect the FTMS client first 
                     # to avoid BlueZ contention during the outgoing connection.
                     if not state.connected_to_ifit:
                         logger.info("📲 FTMS Client Detected but iFit Disconnected. Starting Handoff...")
                         
                         # 1. Parse Handle to Kill
                         handle = None
                         for line in result.stdout.split('\n'):
                             if ("SLAVE" in line or "PERIPHERAL" in line) and "handle" in line:
                                 parts = line.split()
                                 try:
                                     idx = parts.index('handle')
                                     handle = parts[idx+1]
                                     break
                                 except: pass
                         
                         # 2. Reject Connection (Force Disconnect)
                         if handle:
                             logger.info(f"🚫 Rejecting Client (hdl={handle}) to free radio for iFit Connect...")
                             subprocess.run(["sudo", "hciconfig", "hci0", "noleadv"], check=False)
                             subprocess.run(["sudo", "hcitool", "ledc", handle], check=False)
                         
                         # 3. Signal iFit Loop to Connect
                         logger.info("Signal: iFit Connect Requested")
                         state.ftms_client_connected = True
                         state.ftms_last_activity_time = time.time()
                         
                     else:
                         # iFit already connected, accept client normally
                         logger.info(f"📲 FTMS Client Connected! (iFit already active)")
                         state.ftms_client_connected = True
                         state.ftms_last_activity_time = time.time()
            elif "Connections:" in result.stdout and len(result.stdout) > 20:
                 # Debug: Show us what it sees if not SLAVE but has content
                 logger.info(f"HCI Output (Non-SLAVE/Debug): {result.stdout.strip()}")
                 pass
            
            if has_client and not state.ftms_client_connected:
                logger.info("📲 FTMS Client Connected! (Waking up...)")
                state.ftms_client_connected = True
                state.ftms_last_activity_time = time.time()
                
            elif not has_client and state.ftms_client_connected:
                logger.info("📲 FTMS Client Disconnected (Starting Timer...)")
                state.ftms_client_connected = False
                state.ftms_last_activity_time = time.time() # Start timer from disconnect
            
            # Just touch activity time if connected? 
            # No, last_activity is used for disconnect timer. 
            # If connected, we are "active".
            if state.ftms_client_connected:
                 state.ftms_last_activity_time = time.time()
                 
        except Exception as e:
            logger.error(f"Monitor Error: {e}")
            
        await asyncio.sleep(3.0)


# =============================================================================
# MAIN
# =============================================================================
async def mock_client_loop(server: BlessServer):
    logger.info("Starting MOCK Client Loop (Simulation Mode)...")
    state.connected_to_ifit = True
    
    # Simulate Telemetry
    while True:
        # Update# Dummy replace to check file state is safer? No, just view.s
        state.speed_kph = 5.0 # 5 kph
        state.incline_pct = 1.0 # 1%
        state.distance_m += 1
        
        # Process Controls (Log only)
        while not state.control_queue.empty():
             cmd, val = await state.control_queue.get()
             logger.info(f"MOCK CTRL: Type={cmd} Val={val}")
             
        # Trigger FTMS update
        await update_ftms(server)
        await asyncio.sleep(1.0)

if __name__ == "__main__":
    global MOCK_MODE
    import argparse
    parser = argparse.ArgumentParser(description='iFit to FTMS Bridge')
    parser.add_argument('--mock', action='store_true', help='Run in simulation mode')
    parser.add_argument('--debug', action='store_true', help='Enable verbose logging')
    parser.add_argument('--name', type=str, default="mytm", help='Bluetooth name to advertise (default: mytm)')
    parser.add_argument('--pi-mode', action='store_true', help='Enable Raspberry Pi optimizations (Ghost Patch, LomaPi, No-Pair)')
    
    args = parser.parse_args()
    
    MOCK_MODE = args.mock
    DEBUG_MODE = args.debug
    SERVER_NAME = os.environ.get("IFIT_BRIDGE_NAME", "iFitPi") if args.pi_mode else args.name # Default iFitPi for Pi (or env var)
    PI_MODE = args.pi_mode
    
    if DEBUG_MODE:
        logger.setLevel(logging.DEBUG)
        logger.info("DEBUG MODE ENABLED")
        
    if MOCK_MODE:
        logger.warning("!!! RUNNING IN MOCK MODE - NO PHYSICAL CONNECTION !!!")
        
        # Monkey patch start so we run mock loop instead of real one
        original_create_task = asyncio.create_task
        def patched_start_client(coro):
            if coro.__name__ == 'ifit_client_loop':
                return original_create_task(mock_client_loop(coro.cr_frame.f_locals['server']))
        # Handle mock args if needed
        
    try:
        asyncio.run(ftms_server_loop())
    except KeyboardInterrupt:
        logger.info("\nStopped by User")
    except Exception as e:
        logger.error(f"Fatal Error: {e}")
    finally:
        print() # Newline on exit
