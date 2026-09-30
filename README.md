# Solbil tablet

Touch-interface til Raspberry Pi 3B+ med en RTD2011-skærm. Et tryk på en knap sender en CAN FD-kommando til lysprintet.

```
include/   can_fd.h   lights.h   ui.h
src/       can_fd.c   lights.c   ui.c   main.c
scripts/   setup_can.sh  setup_vcan.sh  solbil-tablet.service
```

| Modul      | Ansvar |
|------------|--------|
| `can_fd`   | Åbner SocketCAN i FD mode og sender frames |
| `lights`   | Protokol (kopi af `fdcan_driver.h`) og knaplogik (samme regler som `button.c`) |
| `ui`       | SDL2-vindue, knap-layout og touch-input |
| `main`     | Argumenter, opstart og nedlukning |

## 1. Hardware
Raspberry Pi 3B+ har **ingen indbygget CAN-controller**, så I skal bruge en af følgende:
- En CAN FD HAT med MCP2518FD (fx Waveshare 2-CH CAN FD HAT). Tilføj i `/boot/firmware/config.txt`:
  ```
  dtparam=spi=on
  dtoverlay=mcp251xfd,spi0-0,interrupt=25
  ```
  (Interrupt-pin og oscillator afhænger af HAT'en, så tjek databladet.)
- Eller en USB-adapter med CAN FD-understøttelse (fx PEAK PCAN-USB FD eller candleLight FD), som dukker op som `can0` uden ekstra opsætning.

## 2. SD-kort / OS
Flash **Raspberry Pi OS Lite (64-bit)** med Raspberry Pi Imager. Under "Edit settings" skal I aktivere SSH, sætte brugernavn `pi` og wifi op. SDL2 kan tegne direkte på skærmen (KMSDRM), så I behøver ikke et skrivebordsmiljø.

Installér derefter pakker på Pi'en:
```sh
sudo apt update
sudo apt install build-essential libsdl2-dev libsdl2-ttf-dev fonts-dejavu-core can-utils
```

## 3. Overfør koden fra PC til Pi
Skriv koden på PC'en og kompilér den på Pi'en. Brug en af følgende måder:
- **VS Code Remote-SSH** (anbefalet): Forbind til `pi@<pi-ip>` og redigér filerne direkte på Pi'en.
- **scp** fra PowerShell: `scp -r . pi@<pi-ip>:~/solbil-tablet`
- **Git**: Push til GitHub og kør `git pull` på Pi'en.

## 4. Byg og kør
```sh
cd ~/solbil-tablet
chmod +x scripts/*.sh
make
sudo scripts/setup_can.sh can0   # bitrates skal matche STM32-printet
./build/solbil-tablet            # fullscreen, sender på can0
./build/solbil-tablet -n -w      # dry-run i vindue, til test uden CAN
```
Test uden print: kør `sudo scripts/setup_vcan.sh`, start programmet med `-i vcan0` og kør `candump vcan0` i en anden terminal.

## 5. Autostart ved boot
```sh
sudo cp scripts/solbil-tablet.service /etc/systemd/system/
sudo systemctl enable --now solbil-tablet
```

## Protokol (fra Solbil_rearlights)
CAN ID `0x123`, standard ID, 16 byte CAN FD-frame uden BRS, 1 Mbit/s.
Byte 0 = `lightID`, byte 1 = `command`, resten er 0. Ændres protokollen i
`fdcan_driver.h`, skal `include/lights.h` rettes tilsvarende.

## TODO
- [ ] Find ud af hvilken CAN-hardware der skal sidde på Pi'en
- [ ] Sæt `fdcan.c` på modtagerprintet til `FDCAN_MODE_NORMAL` (står i internal loopback)
- [ ] Husk 120 Ω terminering i begge ender af bussen
