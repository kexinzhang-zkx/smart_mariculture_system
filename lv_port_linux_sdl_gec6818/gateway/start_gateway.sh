#!/bin/sh
# ================================================================
#  start_gateway.sh — GEC6818 网关启动脚本
# ================================================================

echo "[GW] Starting Mosquitto..."
mosquitto -d -c /etc/mosquitto/mosquitto.conf 2>/dev/null
sleep 1

if pidof mosquitto > /dev/null; then
    echo "[GW] Mosquitto OK"
else
    echo "[GW] Mosquitto FAIL, starting anyway..."
fi

echo "[GW] Starting Gateway..."
cd /gateway
./gateway &

echo "[GW] Gateway started (PID: $!)"
echo "[GW] Log: tail -f /var/log/gateway.log"
