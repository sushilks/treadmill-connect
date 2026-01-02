# iFitPi: Raspberry Pi Headless Bridge Setup

This directory contains the scripts and configuration required to turn a Raspberry Pi (Pi 3/4/Zero 2) into a headless Bluetooth Low Energy (BLE) FTMS bridge for iFit treadmills.

Was tested on a "Raspberry Pi 3 Model B Rev 1.2" running "Raspberry Pi OS (64-bit)".

## 🎯 Objective
*   Connect to the Treadmill via Wi-Fi/Console.
*   Bridge connection to BLE (FTMS Protocol).
*   **CRITICAL:** Operate in **LE-Only Mode** ensuring NO PAIRING requests are ever sent to the user's phone.

## 🛡️ The "Castle Keep" Security Architecture
To solve persistent "Pairing Request" dialogs on iOS/Android, we implement a **Triple-Layer Defense** to forcibly disable Bluetooth Bonding.

### Layer 1: System Configuration (Static)
We modify `/etc/bluetooth/main.conf` to set defaults:
*   `ControllerMode = le` (Disable Classic Bluetooth radio)
*   `Pairable = false` (Reject pairing attempts by default)
*   `JustWorksRepairing = always` (Allow non-interactive encryption if forced)

### Layer 2: Boot Enforcement (Kernel)
The `ifit-bridge.service` allows a **Pre-Start** hook (`ExecStartPre`) that runs `runtime_config.sh`.
This script bypasses the daemon and talks directly to the kernel via `btmgmt`:
*   `btmgmt bondable off` (Tells the controller hardware to reject bonding frames)
*   `bluetoothctl pairable off` (Reinforces daemon state)

### Layer 3: Runtime Watchdog (Application)
The Python application (`src/main.py`) contains a `security_watchdog_loop`.
*   Runs every **10 seconds**.
*   Checks `bluetoothctl show`.
*   If `Pairable: yes` is detected (e.g., reset by a library or OS update), it **immediately forces it back to NO**.

---

## 🚀 Installation Guide

### 1. Prerequisites
*   **Verified Hardware:** Raspberry Pi 3 Model B Rev 1.2 (ARMv8).
*   OS: Raspberry Pi OS (64-bit recommended, Lite version).
*   Python 3.9+.

### 2. Dependencies
```bash
sudo apt update
sudo apt install python3-pip git bluez bluez-tools
pip3 install bleak bless inputs
```

### 3. Deploy Code
Copy the project to the Pi (e.g., `~/work/treadmill-connect`).

### 4. Install Systemd Service
This ensures the bridge starts at boot and applies all security layers.

```bash
# 1. Link the service file
sudo cp pi/ifit-bridge.service /etc/systemd/system/

# 2. Reload Systemd
sudo systemctl daemon-reload

# 3. Enable and Start
sudo systemctl enable ifit-bridge
sudo systemctl start ifit-bridge
```

### 5. Configuration (Optional)
If your treadmill is not named "I_TL", you can configure the target name by setting an environment variable in the service file.

1. Edit the service: `sudo systemctl edit ifit-bridge`
2. Add the following lines:
   ```ini
   [Service]
   Environment="IFIT_DEVICE_NAME=NordicTrack 1750"
   ```
3. Save and Restart: `sudo systemctl restart ifit-bridge`

### 6. Apply Static Configuration (One-Time)
Run the improved installer script to lock down `/etc/bluetooth/main.conf`.

```bash
chmod +x pi/install_le_mode.sh
sudo ./pi/install_le_mode.sh
```

---

## 📂 File Manifest

| File                  | Purpose                                                                        |
| :-------------------- | :----------------------------------------------------------------------------- |
| `ifit-bridge.service` | Main Systemd Unit. Handles Auto-Start and Restart on failure.                  |
| `runtime_config.sh`   | **Layer 2 Defense.** Runs *before* the python app to lock controller settings. |
| `startup.sh`          | Wrapper script. Sets up logging to `~/bt_startup.log` and launches Python.     |
| `install_le_mode.sh`  | **Layer 1 Defense.** Updates global `/etc/bluetooth/main.conf`.                |

## 🔧 Troubleshooting

### Check Service Status
```bash
sudo systemctl status ifit-bridge
```

### View Real-Time Logs
```bash
sudo journalctl -u ifit-bridge -f
```

### Verify Security State
Run this to confirm the "Triple Defense" is working. You want to see **Pairable: no** and **ActiveInstances: 1**.
```bash
bluetoothctl show
```
