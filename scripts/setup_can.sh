#!/bin/sh
# Sætter CAN interfacet op i FD mode, så det matcher Solbil_rearlights (fdcan.c):
#   48 MHz, nominel 1+35+12 tq  -> 1 Mbit/s, sample point 75 %
#   data      1+17+6  tq        -> 2 Mbit/s, sample point 75 % (bruges ikke, printet kører uden BRS)
set -e

IFACE=${1:-can0}

ip link set "$IFACE" down 2>/dev/null || true
ip link set "$IFACE" up type can \
    bitrate 1000000 sample-point 0.75 \
    dbitrate 2000000 dsample-point 0.75 \
    fd on restart-ms 100
ip -details link show "$IFACE"
