#!/bin/bash
set -e

# Log to stdout
echo "=== Starting Deployment on $(hostname) ==="

# 1. Setup Workspace
mkdir -p ~/work
cd ~/work

# 2. Clone or Update Config
if [ ! -d "treadmill-connect" ]; then
    echo "Cloning repo..."
    git clone https://github.com/sushilks/treadmill-connect.git
else
    echo "Updating repo..."
    cd treadmill-connect
    git pull
    cd ..
fi

cd treadmill-connect

# 3. Setup Python Environment (Venv)
echo "Setting up Virtual Environment..."
if [ ! -d "venv" ]; then
    python3 -m venv venv
fi
source venv/bin/activate

echo "Installing Dependencies..."
pip install bleak bless inputs

# 4. Install Systemd Service
echo "Installing Service..."
sudo cp pi/ifit-bridge.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ifit-bridge

# 5. Apply Configs
echo "Applying Configurations..."
sudo chmod +x pi/install_le_mode.sh pi/runtime_config.sh pi/startup.sh

# Force LE config (might fail if no hci0 yet, but script handles it)
sudo ./pi/install_le_mode.sh || echo "Warning: LE Config script had issues, check logs."

# 6. Restart Service
echo "Restarting Service..."
sudo systemctl restart ifit-bridge

echo "=== Deployment Complete! ==="
