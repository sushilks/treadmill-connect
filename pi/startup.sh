#!/bin/bash
# pi/startup.sh
# Main entry point for ifit-bridge.service

# 1. Absolute path to project
PROJECT_DIR="/home/pi/work/treadmill-connect"
cd "$PROJECT_DIR" || exit 1

# Redirect stdout/stderr to logfile for debugging
exec > >(tee -a /home/pi/bt_startup.log) 2>&1
echo "=== Booting iFitPi Bridge: $(date) ==="

# 2. Bluetooth is configured by Systemd ExecStartPre (runtime_config.sh)
echo "Bluetooth Controller should be ready."
sleep 1 # Minimal buffer

# 3. Give BlueZ a moment to settle
sleep 2

# 4. Activate Virtual Env (if exists)
if [ -d "venv" ]; then
    source venv/bin/activate
fi

# 5. Start the Bridge
echo "Starting iFitPi Bridge..."
# --pi-mode required for Pi optimization
python3 src/main.py --pi-mode --debug
