#!/bin/sh
# Installerer DTU Roadrunners-opstartsbilledet (Plymouth-tema) i stedet for Raspberry Pi'ens.
#
#   sudo scripts/install_bootsplash.sh          # installér og brug temaet
#   sudo scripts/install_bootsplash.sh restore  # tilbage til Raspberry Pi'ens tema (pix)
#
# Temaet bygges ind i initramfs, så det tager et minut. Genstart bagefter: sudo reboot
set -e

DIR=$(cd "$(dirname "$0")/.." && pwd)
THEME_DIR=/usr/share/plymouth/themes/solbil
SET_THEME=/usr/sbin/plymouth-set-default-theme

if [ "$(id -u)" != 0 ]; then
    echo "Kør med sudo: sudo $0 $1"
    exit 1
fi

if [ "$1" = "restore" ]; then
    $SET_THEME -R pix
    echo "Raspberry Pi'ens opstartsbillede er sat tilbage - genstart med: sudo reboot"
    exit 0
fi

mkdir -p $THEME_DIR
cp "$DIR"/assets/plymouth/solbil.plymouth "$DIR"/assets/plymouth/solbil.script \
   "$DIR"/assets/plymouth/logo.png "$DIR"/assets/plymouth/bar.png \
   "$DIR"/assets/plymouth/track.png $THEME_DIR/

echo "Bygger initramfs med det nye tema (ca. 1 minut)..."
$SET_THEME -R solbil
echo "Opstartsbilledet er installeret - genstart med: sudo reboot"
