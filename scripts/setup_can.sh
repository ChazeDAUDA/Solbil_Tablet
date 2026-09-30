#!/bin/sh
# Sætter CAN interfacet op til klassisk CAN, 1 Mbit/s.
# VESC 100/250 kan ikke CAN FD, så hele bussen kører klassisk CAN.
# Bitrate SKAL være ens på Pi, STM32 (fdcan.c: 48 MHz, 1+35+12 tq = 1 Mbit/s)
# og VESC (VESC Tool -> App Settings -> General -> CAN Baud Rate = CAN_BAUD_1M).
set -e

IFACE=${1:-can0}

ip link set "$IFACE" down 2>/dev/null || true
ip link set "$IFACE" up type can bitrate 1000000 sample-point 0.75 restart-ms 100
ip -details link show "$IFACE"
