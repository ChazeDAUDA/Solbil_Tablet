#!/bin/bash
# Sender falske VESC-statusbeskeder (controller-ID 10), så dashboardet kan testes uden VESC.
# Brug:  sudo scripts/setup_vcan.sh
#        ./build/solbil-tablet -i vcan0      (i én terminal)
#        scripts/fake_vesc.sh vcan0          (i en anden)
# Hastigheden stiger fra 0 til 72 km/h og bliver der i ca. 10 sekunder.

IFACE=${1:-vcan0}

h16() { printf "%04X" $(( ($1) & 0xFFFF )); }
h32() { printf "%08X" $(( ($1) & 0xFFFFFFFF )); }

for i in $(seq 0 150); do
    kmh=$(( i < 60 ? i * 12 / 10 : 72 ))
    erpm=$(( kmh * 7427 / 100 ))   # Passer til 7 polpar og 0,50 m hjul i vesc.h

    cansend "$IFACE" 0000090A#$(h32 $erpm)$(h16 350)$(h16 420)            # STATUS_1: ERPM, motorstrøm, duty
    cansend "$IFACE" 00000E0A#$(h32 44000)$(h32 2000)                    # STATUS_2: 4,4 Ah brugt, 0,2 Ah ladet
    cansend "$IFACE" 00000F0A#$(h32 2305000)$(h32 123000)                # STATUS_3: 230,5 Wh brugt, 12,3 Wh ladet
    cansend "$IFACE" 0000100A#$(h16 280)$(h16 420)$(h16 -186)0000        # STATUS_4: 28 °C, 42 °C, -18,6 A
    cansend "$IFACE" 00001B0A#$(h32 331548)$(h16 504)0000                # STATUS_5: 12,4 km, 50,4 V
    sleep 0.1
done
