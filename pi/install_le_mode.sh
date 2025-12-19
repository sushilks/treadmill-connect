#!/bin/bash
# pi/install_le_mode.sh
# CONFIGURATION SCRIPT
# Run this ONCE to force the Raspberry Pi Bluetooth Daemon into LE-Only mode.
# This prevents it from Advertizing as a Classic Bluetooth device (Speaker/Headphone/etc).

CONFIG_FILE="/etc/bluetooth/main.conf"

echo "Backing up $CONFIG_FILE..."
sudo cp "$CONFIG_FILE" "$CONFIG_FILE.bak"

echo "Configuring Bluetooth for LE-only mode with pairing disabled..."

# 1. Start with a fresh LE configuration
sudo cat <<EOF > /etc/bluetooth/main.conf
[General]
# Force LE Only
ControllerMode = le

# DISABLE PAIRING (The Fix)
Pairable = false

# Enable Discovery by default?
Discoverable = true    
DiscoverableTimeout = 0

# Security (Relaxed)
Privacy = off
JustWorksRepairing = always

[Policy]
AutoEnable = true
EOF

echo "Configuration updated. Verifying:"
grep -E "ControllerMode|Pairable|Discoverable" "$CONFIG_FILE"

echo ""
echo "Restarting Bluetooth Service..."
sudo systemctl restart bluetooth

echo "Done. The Pi should now ONLY appear as a BLE device."
