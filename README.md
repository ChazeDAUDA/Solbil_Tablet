# Solbil tablet

Program til Raspberry Pi 3B+ i solbilen. Fysiske knapper på Pi'ens GPIO tænder og slukker lys. Hvert tryk sendes som en CAN-kommando til STM32-lysprintet ([Solbil_rearlights](https://github.com/CarlFalkenhayn/Solbil_rearlights)). Programmet læser samtidig status fra VESC-motorcontrolleren på samme bus.

```
[Pi + knapper] ── CAN 1 Mbit/s ── [VESC 100/250] ── [STM32 lysprint]
```

Senere kommer et dashboard på skærmen med hastighed, batteri, effekt og lys- og blinklysindikatorer.

```
include/   button.h   can_bus.h   lights.h   vesc.h   time_ms.h
src/       button.c   can_bus.c   lights.c   vesc.c   main.c
scripts/   setup_can.sh  setup_vcan.sh  fake_vesc.sh  solbil-tablet.service
assets/    logo.png   fonts/ (Inter og JetBrains Mono, SIL Open Font License)
```

| Modul    | Ansvar |
|----------|--------|
| `button` | Læser GPIO-knapperne med debounce (50 ms) |
| `lights` | Protokol (kopi af `fdcan_driver.h`) og knaplogik (samme regler som `button.c` på STM32) |
| `can_bus`| SocketCAN: sender og modtager klassiske CAN-frames |
| `vesc`   | Fortolker VESC-status (fart, spænding, strøm, Ah, temperatur) |
| `display`| Dashboardet: hastighed, batteri, effekt, lysindikatorer, dato/tid |
| `gfx`    | Tegne-hjælpere (afrundede kort, buer, cachet tekst) via GPU |
| `main`   | Argumenter, opstart og loop der læser knapper og CAN hvert 10. ms |

## Knapper
Hver knap forbindes mellem GPIO-benet og **GND**. Pi'ens interne pull-up bruges, så der skal ikke bruges modstande.

| Knap       | GPIO (BCM) | Fysisk pin | Opførsel |
|------------|-----------|------------|----------|
| Kørelys    | GPIO 5    | 29         | Tryk = tænd/sluk. Slukker også blinklys |
| Bremse     | GPIO 6    | 31         | Tændt så længe knappen holdes |
| Blink V    | GPIO 13   | 33         | Tryk = tænd/sluk. Kræver kørelys, virker ikke under havari |
| Blink H    | GPIO 19   | 35         | Som Blink V |
| Havari     | GPIO 26   | 37         | Tryk = tænd/sluk. Slukker blinklys |

GND findes fx på fysisk pin 30, 34 eller 39. Pins ændres i tabellen i `src/button.c`. SPI0 (GPIO 8-11) og GPIO 25 er holdt fri til en CAN HAT.

## Byg og kør
```sh
sudo apt install -y git build-essential can-utils libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev fonts-dejavu-core
cd ~/solbil-tablet
chmod +x scripts/*.sh
make
./build/solbil-tablet -n     # dry-run: printer frames, sender intet
./build/solbil-tablet -n -w  # samme, men i et vindue i stedet for fullscreen
```
Ved hvert knaptryk printes CAN-framen og den nye tilstand:
```
[can] TX 0x123 [8] 01 01 00 00 00 00 00 00
[lys]  Kørelys: TÆNDT  Bremse: slukket  Blink V: slukket  Blink H: slukket  Havari: slukket
```

Med CAN-hardware:
```sh
sudo scripts/setup_can.sh can0
./build/solbil-tablet
```
Programmet printer VESC-data hvert sekund:
```
[vesc] id 10   23.4 km/h   48.0 V    12.3 A     590 W   87 %  FET 35°C  motor 40°C
```

### Test uden hardware (vcan)
```sh
sudo scripts/setup_vcan.sh
./build/solbil-tablet -i vcan0
```
Kør i en anden terminal `candump vcan0` for at se lyskommandoerne. Du kan også sende falske VESC-data, så hele dashboardet fyldes ud:
```sh
scripts/fake_vesc.sh vcan0
```
Felter uden data viser `NULL`.

## CAN-bus: klassisk CAN, ikke FD
VESC 100/250 kan **ikke** CAN FD. Hvis der kommer FD-frames på bussen, sender VESC'en error frames, og så forstyrres hele bussen. Derfor kører hele bussen klassisk CAN med **1 Mbit/s**:
- **Pi:** `scripts/setup_can.sh`
- **STM32:** `fdcan.c` er allerede 1 Mbit/s. `FDCAN_FRAME_FD_NO_BRS` modtager også klassiske frames, og modtageren læser kun byte 0-1.
- **VESC:** I VESC Tool under App Settings → General skal I sætte:
  - `CAN Baud Rate` = `CAN_BAUD_1M`
  - `CAN Status Message Mode` = `CAN_STATUS_1_2_3_4_5`
  - `CAN Status Rate` = fx 50 Hz

## VESC-data
| Værdi               | Kilde |
|---------------------|-------|
| Hastighed           | ERPM (STATUS_1) ÷ polpar ÷ gearing × hjulomkreds |
| Batterispænding     | STATUS_5 |
| Batteristrøm        | STATUS_4 (input current) |
| Effekt (W)          | spænding × batteristrøm |
| Batterikapacitet    | Ah brugt minus Ah ladet (STATUS_2) i forhold til `BATTERY_CAPACITY_AH`. Tælleren nulstilles, når VESC'en genstarter |

Motorens polpar, gearing, hjuldiameter og batterikapacitet sættes øverst i `include/vesc.h`.

## CAN-hardware
Raspberry Pi 3B+ har **ingen indbygget CAN-controller**, så I skal bruge en af følgende:
- En CAN FD HAT med MCP2518FD (fx Waveshare 2-CH CAN FD HAT). Tilføj i `/boot/firmware/config.txt`:
  ```
  dtparam=spi=on
  dtoverlay=mcp251xfd,spi0-0,interrupt=25
  ```
  (Interrupt-pin og oscillator afhænger af HAT'en, så tjek databladet.)
- Eller en USB-adapter med CAN FD-understøttelse (fx PEAK PCAN-USB FD eller candleLight FD).

## Protokol (fra Solbil_rearlights)
CAN ID `0x123`, standard ID, klassisk 8-byte frame, 1 Mbit/s.
Byte 0 = `lightID`, byte 1 = `command`, resten er 0. Ændres protokollen i
`fdcan_driver.h`, skal `include/lights.h` rettes tilsvarende.

## Autostart ved boot
```sh
sudo cp scripts/solbil-tablet.service /etc/systemd/system/
sudo systemctl enable --now solbil-tablet
```

## TODO
- [ ] CAN-hardware til Pi'en
- [ ] Sæt `fdcan.c` på modtagerprintet til `FDCAN_MODE_NORMAL` (står i internal loopback)
- [ ] 120 Ω terminering i begge ender af bussen (Pi og STM32 er enderne, VESC sidder i midten)
- [ ] VESC Tool: 1 Mbit/s og status-beskeder 1-5 slået til
- [ ] Ret polpar, gearing, hjuldiameter og batterikapacitet i `include/vesc.h`
- [ ] Dashboard: hastighed, batterispænding/-strøm/-kapacitet, effekt, lys- og blinklysindikatorer
