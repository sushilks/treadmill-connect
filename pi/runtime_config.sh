#!/bin/bash
# pi/runtime_config.sh
# Called by ifit-bridge.service (ExecStartPre) at boot.
# Enforces Bluetooth Controller Settings that cannot be set in main.conf

# 1. Ensure Radio is Unblocked (Fixes RF-kill 132/Timeout)
if command -v rfkill &> /dev/null; then
    rfkill unblock bluetooth
    sleep 1
fi

# 2. Wait for Adapter
# 2. Wait for Adapter (Skipped to avoid HANG)
# for i in {1..10}; do ... done

# 3. Force Controller Flags (Disabled - Let Python Watchdog handle it)
# The Python app (main.py) monitors and enforces "Pairable: no" / "Discoverable: yes".
# This prevents boot-time hangs in ExecStartPre.

echo "Runtime Config: RF-Kill Unblocked. Skipping low-level commands to prevent hang."
exit 0
