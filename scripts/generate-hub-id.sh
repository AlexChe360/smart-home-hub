#!/bin/bash
# Generate unique Hub ID from MAC address
# Usage: ./generate-hub-id.sh

IFACE=$(ip route show default 2>/dev/null | awk '/default/ {print $5}' | head -1)

if [ -z "$IFACE" ]; then
    # Fallback: use eth0 or wlan0
    for iface in eth0 wlan0; do
        if [ -d "/sys/class/net/$iface" ]; then
            IFACE=$iface
            break
        fi
    done
fi

if [ -z "$IFACE" ]; then
    echo "ERROR: No network interface found" >&2
    exit 1
fi

MAC=$(cat "/sys/class/net/$IFACE/address" | tr -d ':' | tr '[:lower:]' '[:upper:]')
echo "HUB-${MAC}"
