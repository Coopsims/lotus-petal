# Building and flashing

## Toolchain

**ESP-IDF 5.4 or newer is required, not preferred.** The display driver this board
needs — `esp_lcd_st77916` v2 — does not exist for 5.3, and dependency resolution
fails outright rather than degrading.

```bash
# macOS prerequisites
brew install cmake ninja dfu-util python@3.13

# ESP-IDF, once
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.2 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.4
~/esp/esp-idf-v5.4/install.sh esp32s3
```

If you cloned without full submodules:

```bash
git -C ~/esp/esp-idf-v5.4 submodule update --init --recursive
```

Load the environment in **every** new shell — it does not persist:

```bash
. ~/esp/esp-idf-v5.4/export.sh
```

> **If `export.sh` reports a missing Python virtual environment**, the Python on
> your `PATH` is not the one ESP-IDF was installed against. It looks for an env
> named after the interpreter it finds, so a system Python 3.9 will send it hunting
> for `idf5.4_py3.9_env` when `~/.espressif/python_env/` actually holds
> `idf5.4_py3.13_env`. Put the right interpreter first and re-source:
>
> ```bash
> export PATH="/opt/homebrew/opt/python@3.13/bin:$PATH"
> . ~/esp/esp-idf-v5.4/export.sh
> ```

## Build

```bash
idf.py -C firmware set-target esp32s3   # first time only; fetches managed components
idf.py -C firmware build
```

`set-target` pulls the managed components named in `firmware/main/idf_component.yml`
from the Espressif registry. A clean build takes a few minutes; incremental builds
are seconds.

Using `-C firmware` rather than `cd firmware` is a habit worth keeping — it works
from the repository root, where the layer check and the docs also live.

The build reports the image size against the slot:

```
lotus-petal.bin binary size 0x165610 bytes. Smallest app partition is 0x300000 bytes. 53% free.
```

## Flash and monitor

```bash
idf.py -C firmware -p /dev/cu.usbmodem101 flash monitor
```

`Ctrl-]` exits the monitor. Flashing writes the bootloader, the partition table and
the app — about nine seconds.

### Finding the port

```bash
ls /dev/cu.usbmodem*     # macOS
ls /dev/ttyACM* /dev/ttyUSB*   # Linux
```

On the reference module the ESP32-S3 appears as **`/dev/cu.usbmodem*`** (native
USB-Serial/JTAG). If your module also carries a separate radio co-processor, that
one appears as `/dev/cu.usbserial-*` — **do not flash it.** The single USB-C port is
muxed between the two chips, so if the `usbmodem` device is absent, **flip the
USB-A→C cable end over** and look again.

Confirm which chip you are talking to:

```bash
esptool --port /dev/cu.usbmodem101 chip-id
```

The S3 reports `ESP32-S3` with embedded PSRAM. Anything else is the wrong chip.

## Useful variants

```bash
# Wipe stored state: saved game, brightness, touch calibration
idf.py -C firmware -p PORT erase-flash

# Back the whole flash up before overwriting a device you care about
esptool --port PORT read-flash 0x0 0x1000000 backup.bin

# Battery-curve calibration build (see docs/HARDWARE.md)
idf.py -C firmware -DLOTUS_BATTERY_CALIB=1 build flash

# Just the app, skipping bootloader and partition table
idf.py -C firmware -p PORT app-flash

# Start over
idf.py -C firmware fullclean
```

## Checks worth running

```bash
sh tools/check-layers.sh
```

Fails if anything under `app/` includes a platform SDK header, a FreeRTOS header or
a board header. The portability claim in the README is only worth something because
something checks it — run this before opening a pull request.

A clean build should produce **no warnings** from project sources. (ESP-IDF's own
mbedtls emits a CMake deprecation notice; that one is not ours.)

## Updating a second device without a cable

Once one dial is flashed:

1. On both: **Settings → Link Petals**, and pair with the PIN. No game needed.
2. On the flashed one: **Settings → Firmware → Send to petal**.
3. On the other: accept the offer when it appears.

The image travels dial-to-dial over the radio and the receiver reboots into it. See
[NETWORKING.md](NETWORKING.md#firmware-transfer) for the protocol, and note the
safety properties: the image goes into the inactive slot, is verified before it is
committed, and is rolled back automatically if it fails to boot.

## Capturing a boot log without the interactive monitor

Useful in scripts and CI. Two control lines matter and are easy to get backwards:

- **DTR drives BOOT/GPIO0** — it must stay *high*, i.e. `dtr = False`. pyserial
  asserts DTR on open by default, which pulls BOOT low and brings the chip up in
  download mode with no app log at all.
- **RTS drives EN/reset** — pulse `rts = True` for ~0.25 s, then release.

```python
import serial, time
s = serial.Serial()
s.port, s.baudrate, s.timeout = "/dev/cu.usbmodem2101", 115200, 0.2
s.dsrdtr = False          # do not touch DTR on open
s.open()
s.dtr = False             # BOOT high -> normal boot
s.rts = True; time.sleep(0.25); s.rts = False   # reset
print(s.read(20000).decode(errors="replace"))
```

pyserial is only present inside the ESP-IDF environment, so source `export.sh`
first. Note that a booted, idle device logs nothing — zero bytes means the reset
did not fire, not that the firmware failed.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `export.sh`: Python venv not found | Wrong Python on `PATH`. See the note above. |
| Dependency resolution fails on `esp_lcd_st77916` | ESP-IDF older than 5.4. |
| No `/dev/cu.usbmodem*` | Cable orientation, on a module with two chips. Flip the USB-A end. |
| Black screen, backlight on | Panel init. Work through the five points in [HARDWARE.md](HARDWARE.md#the-display-recipe). |
| Striped image | Missing `SPICOMMON_BUSFLAG_GPIO_PINS`. |
| Every other pair of rows wrong | Generic init table instead of the `V2` one. |
| Colours inverted or swapped | One of `swap_bytes`, `invert_color`, element order. |
| Dial does nothing at all | Almost certainly a quadrature decode. See [the decode note](HARDWARE.md#the-dial-decode). |
| Touch lands off-target | Settings → Calibrate Touch. |
| Touch feels laggy | `CONFIG_LV_DEF_REFR_PERIOD` above 16. |
| Watchdog reset while opening a screen | LVGL heap exhaustion — check `CONFIG_LV_USE_CLIB_MALLOC=y`. |
| `E gpio: gpio_install_isr_service(): GPIO isr service already installed` | **Benign, expected.** The touch driver installs the shared GPIO interrupt service first; the dial only wants to add a handler to it. `hal_dial.c` tolerates this explicitly — the message is ESP-IDF's, logged at ERROR level for a condition that is normal here. |
| Two dials never see each other | Channel mismatch, or the radio is following an access-point association. |
| App does not fit the partition | Custom partition table not picked up; check `CONFIG_PARTITION_TABLE_CUSTOM`. |
| Large esptool reads fail intermittently | Known flakiness over USB-JTAG on some units. Use the default baud, or enter download mode. |
