#!/bin/sh
# Installerer Waveshare 2-CH CAN FD HAT på Pi'en (én gang, kræver genstart bagefter).
#
#   sudo scripts/install_can_hat.sh
#   sudo reboot
#
# 1. Slår SPI og CAN-driveren (mcp251xfd) til i config.txt - Waveshares "Mode A":
#    HAT'ens CAN_0 sidder på SPI0 (interrupt GPIO 25), CAN_1 på SPI1 (interrupt GPIO 24).
# 2. Giver kanalerne faste navne efter deres fysiske placering på HAT'en. Ellers hedder
#    de can0/can1 i den rækkefølge, chippene bliver klar ved boot - og den kan skifte.
#      HAT CAN_0  ->  can_vesc   (VESC-motorcontrolleren, klassisk CAN)
#      HAT CAN_1  ->  can_stm32  (STM32-lysprintet)
set -e

CONFIG=/boot/firmware/config.txt
LINKDIR=/etc/systemd/network

if [ "$(id -u)" != 0 ]; then
    echo "Kør med sudo: sudo $0"
    exit 1
fi

if grep -q "^dtoverlay=mcp251xfd" $CONFIG; then
    echo "config.txt har allerede CAN-overlays"
else
    cp $CONFIG $CONFIG.before-can
    cat >> $CONFIG <<EOF

# Waveshare 2-CH CAN FD HAT (Mode A: CAN_0 paa SPI0, CAN_1 paa SPI1)
[all]
dtparam=spi=on
dtoverlay=spi1-3cs
dtoverlay=mcp251xfd,spi0-0,interrupt=25
dtoverlay=mcp251xfd,spi1-0,interrupt=24
EOF
    echo "CAN-overlays tilføjet i config.txt (backup: $CONFIG.before-can)"
fi

# Faste navne. *204000 = SPI0 og *215080 = SPI1 (samme adresser på Pi 3 og Pi 4)
mkdir -p $LINKDIR
cat > $LINKDIR/10-can-vesc.link <<EOF
# HAT'ens CAN_0 (SPI0) - VESC-motorcontrolleren
[Match]
Path=platform-*204000.spi-cs-0

[Link]
Name=can_vesc
EOF
cat > $LINKDIR/10-can-stm32.link <<EOF
# HAT'ens CAN_1 (SPI1) - STM32-lysprintet
[Match]
Path=platform-*215080.spi-cs-0

[Link]
Name=can_stm32
EOF
echo "Faste navne: CAN_0 = can_vesc, CAN_1 = can_stm32"
echo "Genstart med: sudo reboot"
