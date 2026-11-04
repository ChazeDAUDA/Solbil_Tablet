# Solbil tablet

Program til Raspberry Pi 3B+ i solbilen. Fysiske knapper på Pi'ens GPIO tænder og slukker lys. Hvert tryk sendes som en CAN-kommando til STM32-lysprintet ([Solbil_rearlights](https://github.com/CarlFalkenhayn/Solbil_rearlights)). Programmet læser samtidig status fra VESC-motorcontrolleren på samme bus.

```
                ┌── can_vesc  (klassisk CAN, 500 kbit/s) ── [VESC 100/250]
[Pi + CAN HAT] ─┤
                └── can_stm32 (1 Mbit/s, CAN FD senere) ──── [STM32 lysprint]
```

Senere kommer et dashboard på skærmen med hastighed, batteri, effekt og lys- og blinklysindikatorer.

```
include/   button.h   can_bus.h   display.h   gfx.h   lights.h   vesc.h   time_ms.h
src/       button.c   can_bus.c   display.c   gfx.c   lights.c   vesc.c   main.c
scripts/   install_can_hat.sh  setup_can.sh  setup_vcan.sh  fake_vesc.sh  autostart.sh  solbil-tablet.service
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
| Kørelys    | GPIO 5    | 29         | Tryk = tænd/sluk |
| Bremse     | GPIO 6    | 31         | Kontakt på bremsepedalen. Tændt så længe den er trykket. Bremselyset tænder også automatisk ved regenerativ bremsning |
| Blink V    | GPIO 13   | 33         | Tryk = tænd/sluk. Virker ikke under havari |
| Blink H    | GPIO 22   | 15         | Som Blink V |
| Havari     | GPIO 26   | 37         | Tryk = tænd/sluk. Slukker blinklys |
| Fjernlys   | GPIO 27   | 13         | Tryk = tænd/sluk. Kræver kørelys |
| Tågelys    | GPIO 23   | 16         | Tryk = tænd/sluk. Kræver kørelys |

GND findes fx på fysisk pin 14, 30, 34 eller 39. Pins ændres i tabellen i `src/button.c`. Benene holder CAN HAT'en fri: SPI0 (GPIO 7-11), SPI1 (GPIO 16-21) og interrupts (GPIO 24-25).

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

Med CAN-hardware (servicen gør det selv ved opstart):
```sh
sudo systemctl stop solbil-tablet   # hvis autostart kører
sudo scripts/setup_can.sh
./build/solbil-tablet
```
Programmet printer VESC-data hvert sekund:
```
[vesc] id 10   23.4 km/h   48.0 V    12.3 A     590 W   87 %  FET 35°C  motor 40°C
```

### Test uden hardware (vcan)
```sh
sudo scripts/setup_vcan.sh
./build/solbil-tablet -v vcan0 -i vcan0
```
Kør i en anden terminal `candump vcan0` for at se lyskommandoerne. Du kan også sende falske VESC-data, så hele dashboardet fyldes ud:
```sh
scripts/fake_vesc.sh vcan0
```
Felter uden data viser `NULL`.

## CAN-busser
VESC 100/250 kan **ikke** CAN FD. Kommer der FD-frames på dens bus, sender den error frames, og så forstyrres hele bussen. Derfor har Pi'en **to separate busser** via CAN HAT'en:

| Bus | HAT-terminal | Til | Hastighed |
|---|---|---|---|
| `can_vesc` | CAN_0 | VESC-motorcontrolleren | 500 kbit/s klassisk (skal matche VESC Tool) |
| `can_stm32` | CAN_1 | STM32-lysprintet | 1 Mbit/s klassisk (CAN FD senere) |

Hastighederne står i `scripts/setup_can.sh`, som servicen kører ved opstart.

**VESC Tool** (App Settings → General):
- `CAN Baud Rate` = `CAN_BAUD_500K` (eller ret `VESC_BITRATE` i `setup_can.sh`)
- `CAN Status Message Mode` = `CAN_STATUS_1_2_3_4_5`
- `CAN Status Rate` = fx 50 Hz

Se trafikken på en bus: `candump can_vesc` (kan køre samtidig med dashboardet).

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
**Waveshare 2-CH CAN FD HAT** (2× MCP2518FD, 40 MHz, galvanisk isoleret), "Mode A": CAN_0 på SPI0 (interrupt GPIO 25), CAN_1 på SPI1 (interrupt GPIO 24).

Installation (én gang):
```sh
sudo scripts/install_can_hat.sh
sudo reboot
```
Scriptet slår SPI og driveren til i `config.txt` og giver kanalerne **faste navne** efter deres placering på HAT'en (`/etc/systemd/network/10-can-*.link`). Uden det hedder de `can0`/`can1` i den rækkefølge, chippene bliver klar ved boot, og den kan skifte.

Tjek: `ip -br link | grep can` skal vise `can_vesc` og `can_stm32`.

**Terminering:** Hver bus skal have 120 Ω i begge ender. HAT'en har en jumper pr. kanal. Mål med strømmen slukket mellem CAN-H og CAN-L: **60 Ω** er rigtigt (to modstande), 120 Ω betyder, at der mangler én.

## Protokol (fra Solbil_rearlights)
CAN ID `0x123`, standard ID, klassisk 8-byte frame, 1 Mbit/s på `can_stm32`.
Byte 0 = `lightID`, byte 1 = `command`, resten er 0. Ændres protokollen i
`fdcan_driver.h`, skal `include/lights.h` rettes tilsvarende.

## Autostart ved boot (kiosk-tilstand)
I bilen skal Pi'en starte direkte i dashboardet uden tastatur, skrivebord eller boot-tekst.
```sh
sudo scripts/autostart.sh on    # til bilen
sudo scripts/autostart.sh off   # tilbage til skrivebordet, til udvikling
sudo reboot
```
- Dashboardet køres af systemd-servicen `solbil-tablet` og tegner direkte på skærmen (KMS/DRM).
- Går programmet ned, starter det igen efter 2 sekunder.
- Mangler CAN-hardwaren, kører dashboardet alligevel og prøver at få forbindelse hvert 2. sekund.
- SSH virker stadig. Under udvikling: `sudo systemctl stop solbil-tablet`, test, og `sudo systemctl start solbil-tablet`.
- Log: `journalctl -u solbil-tablet -f`

**Beskyt SD-kortet**, når alt er færdigt: Bilen slukker uden at lukke Pi'en ned, og det kan ødelægge SD-kortet. Slå skrivebeskyttelse til med `sudo raspi-config nonint do_overlayfs 0` og genstart. Ændringer forsvinder så ved genstart, så slå den fra igen (`do_overlayfs 1`) før I opdaterer koden.

## Regler (iESC 2026)
| Regel | Krav | Løsning |
|---|---|---|
| 2.24.12 | Blinklys 90 ± 30 blink/min | `BLINK_INTERVAL_MS` = 333 ms = 90 blink/min (også i STM32-koden) |
| 2.24.14 | Stoplys ved bremsning, også regenerativ | Pedalkontakt på Bremse-indgangen + automatisk ved negativ batteristrøm fra VESC'en |
| 3.31.2 | Blink til højre når man bliver overhalet | Blinklys virker uden kørelys |
| 2.26.1 | Vis hastighed, blinklys og havari | Dashboardet |

## TODO
- [x] CAN-hardware til Pi'en (Waveshare 2-CH CAN FD HAT)
- [ ] 120 Ω terminering i begge ender af hver bus
- [ ] CAN FD på `can_stm32` (16-byte frames som i `fdcan_driver.c`)
- [ ] VESC Tool: 500K og status-beskeder 1-5 slået til
- [ ] Ret polpar, gearing, hjuldiameter og batterikapacitet i `include/vesc.h`
- [ ] Dashboard: hastighed, batterispænding/-strøm/-kapacitet, effekt, lys- og blinklysindikatorer
