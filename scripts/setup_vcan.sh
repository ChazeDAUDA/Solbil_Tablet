#!/bin/sh
# Opretter et virtuelt CAN interface (vcan0) til test uden hardware.
# Test: ./build/solbil-tablet -i vcan0   og i en anden terminal:  candump vcan0
set -e

modprobe vcan
ip link add dev vcan0 type vcan 2>/dev/null || true
ip link set vcan0 up
