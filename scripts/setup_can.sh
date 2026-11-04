#!/bin/sh
# Sætter CAN-busserne op. Køres automatisk af solbil-tablet-servicen ved opstart.
#
# Hver bus har sin egen hastighed:
#   can_vesc   VESC_BITRATE - SKAL være den samme som i VESC Tool
#              (App Settings -> General -> CAN Baud Rate). VESC'ens standard er 500K.
#   can_stm32  1 Mbit/s klassisk CAN, som fdcan.c på STM32 (48 MHz, 1+35+12 tq).
#              Skiftes til CAN FD, når lysprintet er klar til det.
#
# En bus, der ikke findes (fx hvis HAT'en mangler), springes over.

VESC_BITRATE=500000
STM32_BITRATE=1000000

setup() {
    iface=$1
    shift
    if ! ip link show "$iface" >/dev/null 2>&1; then
        echo "$iface findes ikke - springer over"
        return
    fi
    ip link set "$iface" down
    ip link set "$iface" up type can "$@" restart-ms 100
    echo "$iface oppe: $*"
}

setup can_vesc  bitrate $VESC_BITRATE  sample-point 0.875
setup can_stm32 bitrate $STM32_BITRATE sample-point 0.75
