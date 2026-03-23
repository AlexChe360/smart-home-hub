
#!/bin/bash
set -euo pipefail

# ─── SmartHome Hub — Full Installation Script ───
# Run on Raspberry Pi 4 with Raspberry Pi OS (64-bit recommended)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

log()   { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
error() { echo -e "${RED}[✗]${NC} $*"; exit 1; }

# ─── Check prerequisites ───
if [ "$(id -u)" -ne 0 ]; then
    error "Run as root: sudo $0"
fi

ARCH=$(uname -m)
log "Architecture: $ARCH"

# ─── 1. System packages ───
log "Installing system dependencies..."
apt-get update
apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libmosquitto-dev \
    libcurl4-openssl-dev \
    mosquitto \
    mosquitto-clients \
    libwebsockets-dev \
    libssl-dev \
    uuid-dev \
    git \
    curl \
    jq

# ─── 2. Create smarthome user ───
if ! id -u smarthome &>/dev/null; then
    log "Creating smarthome user..."
    useradd -r -s /usr/sbin/nologin -d /var/lib/smarthome smarthome
    mkdir -p /var/lib/smarthome
    chown smarthome:smarthome /var/lib/smarthome
fi
usermod -a -G dialout smarthome

# ─── 3. Configure Mosquitto for local-only access ───
log "Configuring Mosquitto..."
cat > /etc/mosquitto/conf.d/smarthome.conf << 'EOF'
# SmartHome Hub — Local MQTT only
listener 1883 127.0.0.1
allow_anonymous true
max_connections 50
persistence true

# Logging
log_dest syslog
log_type warning
log_type error
EOF

systemctl enable mosquitto
systemctl restart mosquitto
log "Mosquitto configured (localhost only)"

# ─── 4. Install Zigbee2MQTT ───
log "Installing Zigbee2MQTT..."
if [ ! -d /opt/zigbee2mqtt ]; then
    # Install Node.js 20 LTS
    if ! command -v node &>/dev/null; then
        curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
        apt-get install -y nodejs
    fi

    git clone --depth 1 https://github.com/Koenkk/zigbee2mqtt.git /opt/zigbee2mqtt
    cd /opt/zigbee2mqtt
    # Install pnpm
    if ! command -v pnpm &>/dev/null; then
        npm install -g pnpm
    fi

    # Install dependencies
    pnpm install
        cd "$SCRIPT_DIR"
    else
    warn "Zigbee2MQTT already installed, skipping..."
fi

# Copy Z2M configuration
mkdir -p /opt/zigbee2mqtt/data
if [ ! -f /opt/zigbee2mqtt/data/configuration.yaml ]; then
    cp "$PROJECT_DIR/config/zigbee2mqtt.yaml" /opt/zigbee2mqtt/data/configuration.yaml
    log "Zigbee2MQTT configuration installed"
else
    warn "Z2M config exists, not overwriting"
fi

chown -R smarthome:smarthome /opt/zigbee2mqtt

# Z2M systemd service
cat > /etc/systemd/system/zigbee2mqtt.service << 'EOF'
[Unit]
Description=Zigbee2MQTT
After=network-online.target mosquitto.service
Requires=mosquitto.service

[Service]
Type=simple
User=smarthome
WorkingDirectory=/opt/zigbee2mqtt
ExecStart=/usr/bin/node index.js
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=zigbee2mqtt

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable zigbee2mqtt

# ─── 5. Build SmartHome Hub daemon ───
log "Building SmartHome Hub daemon..."
cd "$PROJECT_DIR"

# Download cJSON (MIT licensed, lightweight JSON parser)
if [ ! -f src/cJSON.c ] || [ "$(wc -l < src/cJSON.c)" -lt 100 ]; then
    log "Downloading cJSON..."
    curl -sL -o src/cJSON.c https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
    curl -sL -o include/cJSON.h https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h
fi

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install binary
make install
log "Hub daemon installed to /usr/local/bin/smarthome-hub"

# ─── 6. Configuration ───
mkdir -p /etc/smarthome
if [ ! -f /etc/smarthome/hub.conf ]; then
    cp "$PROJECT_DIR/config/hub.conf.example" /etc/smarthome/hub.conf
fi

# Generate unique Hub ID from MAC address
IFACE=$(ip route show default | awk '/default/ {print $5}' | head -1)
MAC=$(cat /sys/class/net/"$IFACE"/address 2>/dev/null | tr -d ':' | tr '[:lower:]' '[:upper:]')

if [ -n "$MAC" ]; then
    if grep -q '^hub_id=HUB-0\+$' /etc/smarthome/hub.conf; then
        sed -i "s/^hub_id=HUB-0\+$/hub_id=HUB-${MAC}/" /etc/smarthome/hub.conf
        log "Hub ID: HUB-${MAC}"
    else
        warn "hub_id already set, skipping"
    fi
fi

# Generate random token only if still default
if grep -q '^hub_token=change-me-during-setup$' /etc/smarthome/hub.conf; then
    TOKEN=$(openssl rand -hex 32)
    sed -i "s/^hub_token=change-me-during-setup$/hub_token=${TOKEN}/" /etc/smarthome/hub.conf
    log "Hub token generated"
fi

chown -R smarthome:smarthome /etc/smarthome

# ─── 7. Install systemd service ───
cp "$PROJECT_DIR/systemd/smarthome-hub.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable smarthome-hub

# ─── 8. Create log file ───
touch /var/log/smarthome-hub.log
chown smarthome:smarthome /var/log/smarthome-hub.log

# ─── 9. USB serial permissions ───
# udev rule for Sonoff Dongle
cat > /etc/udev/rules.d/99-sonoff-dongle.rules << 'EOF'
# Sonoff Zigbee 3.0 USB Dongle Plus (Silicon Labs CP2102N)
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="ttyZigbee", MODE="0660", GROUP="dialout"
# Sonoff Zigbee 3.0 USB Dongle Plus-E (CH9102)
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d4", SYMLINK+="ttyZigbee", MODE="0660", GROUP="dialout"
EOF
udevadm control --reload-rules
udevadm trigger

# ─── 10. Start services ───
log "Starting services..."
systemctl start mosquitto
systemctl start zigbee2mqtt
sleep 3
systemctl start smarthome-hub

# ─── Done ───
echo ""
echo "============================================"
echo "  SmartHome Hub Installation Complete!"
echo "============================================"
echo ""
echo "  Hub ID:     $(grep hub_id /etc/smarthome/hub.conf | head -1 | cut -d= -f2 | xargs)"
echo "  Config:     /etc/smarthome/hub.conf"
echo ""
echo "  Services:"
echo "    mosquitto:       $(systemctl is-active mosquitto)"
echo "    zigbee2mqtt:     $(systemctl is-active zigbee2mqtt)"
echo "    smarthome-hub:   $(systemctl is-active smarthome-hub)"
echo ""
echo "  Logs:"
echo "    journalctl -u smarthome-hub -f"
echo "    journalctl -u zigbee2mqtt -f"
echo ""
echo "  Next steps:"
echo "    1. Connect Sonoff Dongle to USB"
echo "    2. Update cloud_host in /etc/smarthome/hub.conf"
echo "    3. sudo systemctl restart smarthome-hub"
echo ""
