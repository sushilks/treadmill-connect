#!/bin/bash
# pi/runtime_config.sh
# Called by ifit-bridge.service (ExecStartPre) at boot.
# Enforces Bluetooth Controller Settings that cannot be set in main.conf

# 1. Wait for Adapter (Critical for Boot Timing)
for i in {1..10}; do
    if hciconfig hci0 > /dev/null 2>&1; then
        break
    fi
    sleep 1
done

# 2. Reset (Ensures clean state)
hciconfig hci0 down
hciconfig hci0 up

# 3. Force Controller Flags (Low Level)
btmgmt -i hci0 power off
btmgmt -i hci0 le on
btmgmt -i hci0 bredr off
btmgmt -i hci0 power on

# 4. Configure Daemon State (High Level)
# We use bluetoothctl to ensure bluetoothd (the daemon) accepts the state
# identifying the "Controller" 
bluetoothctl <<EOF
power on
discoverable on
pairable off
agent NoInputNoOutput
default-agent
EOF

# 5. Force Kernel Flag (Double Ensure)
# bluetoothctl might not clear the HCI flag, so we force it here.
sudo btmgmt -i hci0 bondable off

echo "Runtime Config Applied: LE-Only, Pairable=OFF (Daemon+Kernel Enforced)."
