#!/bin/sh
# Slår autostart (kiosk-tilstand) til eller fra.
#
#   sudo scripts/autostart.sh on    Pi'en starter direkte i dashboardet - uden skrivebord,
#                                   uden boot-tekst og uden login. Til bilen.
#   sudo scripts/autostart.sh off   Tilbage til skrivebordet. Til udvikling.
#
# Genstart bagefter med: sudo reboot
# SSH virker i begge tilstande.
set -e

DIR=$(cd "$(dirname "$0")/.." && pwd)
SERVICE=solbil-tablet
CMDLINE=/boot/firmware/cmdline.txt
CONFIG=/boot/firmware/config.txt
QUIET="quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0"
RUN_USER=${SUDO_USER:-solarcarpi}

if [ "$(id -u)" != 0 ]; then
    echo "Kør med sudo: sudo $0 $1"
    exit 1
fi

case "$1" in
on)
    # Service-filen har stien og brugeren skrevet ind - tilpas til denne Pi
    sed -e "s#/home/solarcarpi/solbil-tablet#$DIR#g" -e "s#^User=.*#User=$RUN_USER#" \
        "$DIR/scripts/$SERVICE.service" > /etc/systemd/system/$SERVICE.service
    systemctl daemon-reload
    systemctl enable $SERVICE

    # Boot til konsol i stedet for skrivebord
    raspi-config nonint do_boot_behaviour B1

    # Skjul boot-tekst, logo og blinkende markør (cmdline.txt er én lang linje)
    grep -q "logo.nologo" $CMDLINE || sed -i "1 s/\$/ $QUIET/" $CMDLINE
    sed -i "s/console=tty1/console=tty3/" $CMDLINE
    grep -q "^disable_splash=1" $CONFIG || echo "disable_splash=1" >> $CONFIG

    echo "Autostart slået TIL - genstart med: sudo reboot"
    ;;
off)
    systemctl disable --now $SERVICE 2>/dev/null || true

    # Tilbage til skrivebord med automatisk login
    raspi-config nonint do_boot_behaviour B4

    sed -i "s/ $QUIET//" $CMDLINE
    sed -i "s/console=tty3/console=tty1/" $CMDLINE

    echo "Autostart slået FRA - genstart med: sudo reboot"
    ;;
*)
    echo "Brug: sudo $0 on|off"
    exit 1
    ;;
esac
