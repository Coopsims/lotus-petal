# Lotus Petal

Firmware for a round, knob-and-touchscreen life counter for trading card games —
built for Commander, where four people each need to track a life total, commander
damage from every opponent, a dozen kinds of token, and whose turn it is.

Turn the dial to change a number. Tap to choose. Swipe to change screen. Put two
or more of them on the same table and they find each other over the radio, so
every player sees the whole pod on their own rim.

It runs on **any device with a dial, a touchscreen and a radio**. The code is
split into two halves along exactly that line:

| | |
|---|---|
| **`firmware/main/hal/`** | Every line that touches a pin, a peripheral or a vendor SDK. |
| **`firmware/main/app/`** | The counters, the screens, the rules, the protocol. Portable C + LVGL. |

The only thing that crosses between them is
[`petal_hal.h`](firmware/main/hal/petal_hal.h). Porting to different hardware
means writing a new implementation of that one header and leaving `app/`
untouched — [`tools/check-layers.sh`](tools/check-layers.sh) fails the build if
anyone quietly breaks that promise.

## What it does

- **Life** — a big total inside a 270° ring that shortens and slides green → red
  as life falls. Battery at the top, turn readout above the number, a Pass button
  in the ring's gap, poison shown only once you have any.
- **Commander damage** — tap the face for a wedge per seat, 0–21, red at 21.
  Linked to life: dialling damage up takes your life down by the same amount, and
  dialling it back restores it.
- **Counters** — a scrollable grid of Treasure, Food, Clue, Blood, two commander
  tax rows (which show the derived +2 per cast), Poison, Energy, Experience,
  Storm, Monarch, Initiative, the Ring's four levels, and Day/Night.
- **Remote play** — dials link with a four-digit PIN, roll a d20 to settle who
  goes first, pick their seats, and then show the whole table's life around the
  rim with the active player in gold. The turn passes from dial to dial, skipping
  anyone who has bowed out. Players without a dial still get a seat.
- **Tools** — hold the middle of the face for a radial menu: Undo turn, Settings,
  Reset, and a dice roller (coin through d100, plus an arbitrary dN you dial in).
- **Settings** — brightness, on-device touch calibration, pairing, firmware
  transfer, and an About page.
- **It remembers** — the whole game is snapshotted as you play, so a flat battery
  mid-game costs you nothing.
- **Firmware travels dial to dial** — flash one over USB and it hands the image to
  the others over the same radio link they use to play. No cable, no server, no
  network.

## Quick start

You need **ESP-IDF 5.4 or newer** — the display driver this board wants does not
exist in 5.3.

```bash
. ~/esp/esp-idf/export.sh
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
idf.py -C firmware -p /dev/cu.usbmodem101 flash monitor
```

Full toolchain setup, port selection and troubleshooting: [docs/BUILD.md](docs/BUILD.md).

## Documentation

| | |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | The two layers, what runs on which thread, and the boot sequence. **Start here.** |
| [docs/HAL.md](docs/HAL.md) | The hardware contract, function by function, and what an implementation has to guarantee. |
| [docs/PORTING.md](docs/PORTING.md) | Bringing this up on different hardware, in the order that hurts least. |
| [docs/APPLICATION.md](docs/APPLICATION.md) | Every screen, the input model, the game model, and persistence. |
| [docs/NETWORKING.md](docs/NETWORKING.md) | Both wire protocols: the game link and the firmware transfer. |
| [docs/HARDWARE.md](docs/HARDWARE.md) | The reference board: pinout, the display recipe, the dial decode, the battery curve. |
| [docs/BUILD.md](docs/BUILD.md) | Toolchain, build, flash, monitor, and what to do when it goes wrong. |
| [IDEAS.md](IDEAS.md) | Considered and not built. |

## Repository layout

```
lotus-petal/
├── docs/                     see the table above
├── tools/
│   └── check-layers.sh       fails if app/ reaches past petal_hal.h
└── firmware/
    ├── CMakeLists.txt        project + app version
    ├── partitions.csv        two app slots, so a dial cannot be bricked
    ├── sdkconfig.defaults    board, PSRAM, LVGL, power settings
    └── main/
        ├── hal/                       ── THE HARDWARE HALF ──
        │   ├── petal_hal.h            the contract; the only door between halves
        │   ├── petal_config.h         geometry + capability flags the app may know
        │   ├── boards/                pins and panel init tables
        │   └── esp32s3/               the ESP32-S3 implementation, one file per device
        └── app/                       ── THE SOFTWARE HALF ──
            ├── app_main.c             what exists, what drives it, what boots
            ├── model/                 counters, game rules, persistence
            ├── net/                   the link protocol, the firmware transfer
            └── ui/                    screen manager, overlay stack, and the screens
```

## The reference hardware

A 360×360 round IPS panel with capacitive touch and a detented rotary knob, on an
ESP32-S3 with 16 MB of flash and 8 MB of PSRAM. Several vendors sell this module;
[docs/HARDWARE.md](docs/HARDWARE.md) has the pinout, the exact display bring-up
recipe (which took real effort to find), and the knob decode — which is not the
one you would expect, and gets you nothing at all if you guess.

## Contributing

The layer boundary is the one rule worth being strict about: if a change under
`app/` needs something from the hardware, add it to `petal_hal.h` and implement it
per platform, rather than reaching around. Run `sh tools/check-layers.sh` before
opening a pull request.

## Licence

Apache License 2.0 — see [LICENSE](LICENSE). You may use, modify and distribute
this freely, including commercially; the licence also grants you a patent licence
from every contributor, and asks that modified files say they were changed.

Not affiliated with, endorsed by, or connected to Wizards of the Coast. *Magic:
The Gathering* and *Commander* are their trademarks; this is an independent
counter that happens to be useful for their game.
