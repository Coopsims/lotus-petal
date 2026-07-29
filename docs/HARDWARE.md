# The reference hardware

Everything on this page is specific to one board. It is here because the display
bring-up and the dial decode both took real effort to work out, and because a port
to anything else needs to know which of these facts are physics and which are
this-module-only.

If you are bringing up different hardware, read this for the shape of the problems,
then see [PORTING.md](PORTING.md).

## What the board is

A round knob-and-touch module from the JC3636W518 family — sold under several
names, and the same panel Waveshare's ESP32-S3-Knob-Touch-LCD-1.8 uses.

| | |
|---|---|
| **SoC** | ESP32-S3R8 — 16 MB flash, 8 MB octal PSRAM |
| **Display** | ST77916, 360×360 round IPS, QSPI |
| **Touch** | CST816 capacitive, I²C |
| **Dial** | Detented rotary encoder, two GPIOs, no push-button on this unit |
| **Power** | Single Li-ion cell behind a 10K/10K divider; no charge-status line |

Some modules in this family carry a second, separate radio co-processor. It is not
used: ESP-NOW runs on the S3's own radio. If your module has one, leave it alone —
and note that a single USB-C port muxed to two chips means the cable orientation
decides which one you are talking to (see [BUILD.md](BUILD.md#finding-the-port)).

## Pinout

| Function | GPIO | Notes |
|---|---|---|
| LCD QSPI SCK | 13 | |
| LCD QSPI CS | 14 | |
| LCD QSPI D0–D3 | 15, 16, 17, 18 | |
| LCD reset | 21 | active low |
| LCD backlight | 47 | drive high = on; PWM-capable |
| Touch I²C SDA / SCL | 11 / 12 | CST816 |
| Touch INT / RST | 9 / 10 | |
| Encoder A / B | 8 / 7 | |
| Battery sense | 1 | ADC1_CH0, 12 dB attenuation, ×2 divider |
| Knob push-button | 48 | **not fitted on this unit** — left untouched |

All verified on-device. They live in
[`hal/boards/board_round360_knob.h`](../firmware/main/hal/boards/board_round360_knob.h).

## The display recipe

A clean image needs **all five** of these together. Miss any one and you get a black
screen, a backlight-only blank, or a striped or interlaced image — and the failure
modes look similar enough that it is very easy to fix one thing, see no
improvement, and conclude it was wrong.

1. **The pins above.** An earlier reading had the backlight on GPIO 15; it is 47.
   (15 looked plausible because the panel appears lit whenever reset leaves 47 high.)

2. **`esp_lcd_st77916` v2.** This is the whole reason the project requires ESP-IDF
   5.4 or newer — 5.3 cannot resolve it.

3. **`SPICOMMON_BUSFLAG_GPIO_PINS` in the bus flags**, alongside `MASTER` and
   `QUAD`. It forces GPIO-matrix routing for pins that are not on the IO_MUX.
   Without it the image stripes. The driver's own `ST77916_PANEL_BUS_QSPI_CONFIG`
   macro sets **no flags at all**, so this must be built by hand — which is why
   `hal_display.c` fills in `spi_bus_config_t` itself.

4. **The `JC3636W518V2` init sequence**, not the generic `JC3636W518` one. The
   generic table produces a two-on-two-off dual-gate interlace: recognisably an
   image, but every other pair of rows wrong. The table lives in
   [`hal/boards/st77916_init.h`](../firmware/main/hal/boards/st77916_init.h), taken
   from ESPHome's `mipi_spi` model of the same name. `SLPOUT` (0x11) is appended
   because the driver sends MADCTL and COLMOD itself but expects sleep-out to come
   from the table.

5. **Colour: RGB565 byte-swapped, `invert_color(true)`, RGB element order.** For
   LVGL, use the port's `swap_bytes` flag rather than swapping pixels by hand.

Sources that unlocked this: ESPHome's `mipi_spi` model `JC3636W518V2`,
[KrX3D/WaveShare-Knob-Esp32S3](https://github.com/KrX3D/WaveShare-Knob-Esp32S3)
(working config plus schematics), and the QSPI thread on
[espressif/esp-bsp#764](https://github.com/espressif/esp-bsp/issues/764).

Also worth knowing: `CONFIG_LV_DEF_REFR_PERIOD=16`. The 33 ms default is what makes
touch feel laggy on this panel.

## The dial decode

**This knob is not a textbook quadrature encoder, and decoding it as one reports
nothing at all.** The failure is silent — no wrong counts, no jitter, just a dead
dial that looks like broken wiring.

A raw GPIO capture of one slow rotation shows what actually happens. Rest is **both
lines high**:

```
A=1 B=1     rest
A=0 B=1     A departs — the PRIMARY pulse, dwelling low for tens of ms
A=1 B=1     A back at rest
A=1 B=0     B blips low ~0.2 ms later — a SECONDARY, ~0.2 ms wide
A=1 B=1     B back at rest
            long quiet gap, then the next detent
```

Counter-clockwise is the mirror image: primary on B, secondary blip on A.

So the usual "A-fall = +1, B-fall = −1" accumulator scores +1 and then −1 for every
single detent, nets **zero**, and never reports a thing. No amount of time-based
debouncing rescues it either — the two edges are about 34 ms apart, so any window
wide enough to swallow the secondary also swallows real detents.

What works: **decide direction by which line leaves rest first**, and accept the
count only if the knob had been at rest for at least 1.2 ms just before departing.
That single gate discards both the 0.2 ms secondary blip and the ~0.6 ms
contact-bounce re-fires, while real detents — milliseconds apart — always qualify.

```c
/* On a GPIO ANYEDGE interrupt for BOTH A and B. No polling. */
if (a == 1 && b == 1) {                          /* entered rest */
    if (!at_rest) { at_rest = 1; rest_enter = now; }
} else if (at_rest) {                            /* just left rest */
    at_rest = 0;
    if (now - rest_enter >= 1200) {              /* preceded by a real rest */
        if      (a == 0 && b == 1) delta++;      /* A left first = clockwise */
        else if (a == 1 && b == 0) delta--;      /* B left first = the other way */
    }
}
```

The full version is [`hal/esp32s3/hal_dial.c`](../firmware/main/hal/esp32s3/hal_dial.c).
To reverse the direction, swap the `++` and `--`.

Steps per rotation is the knob's physical detent count, roughly 20–30. Firmware
cannot add resolution — only avoid dropping what is there.

It is **interrupt-driven rather than polled**, which is a real battery saving: the
previous implementation woke the CPU 5,000 times a second to read two pins that
change a handful of times a second.

## Touch

The CST816 works as an ordinary `esp_lcd_touch` pointer device, but two things about
it shape the app:

**Its gesture reporting is unreliable.** Swipes are detected in the app from
press-to-release displacement instead. Also clear `LV_OBJ_FLAG_SCROLLABLE` on
full-face touch targets, or drags get consumed as scrolling before you ever see
them.

**Raw coordinates drift towards the edges, differently on every unit.** Hence the
per-device linear map (`display = scale × raw + offset` per axis) applied inside
the driver, and Settings → Calibrate Touch to fit it. The board's baked-in defaults
are accurate at the centre and drift at the edges — enough to reach the
calibration screen, not enough to enjoy using.

## Battery

Cell → 10K/10K divider → GPIO 1 (ADC1_CH0), so the pin reads half the cell voltage.
12 dB attenuation, ×2 in software, averaged over 8 samples because a single read on
this part visibly moves the gauge.

**There is no charge-status line to the SoC.** While plugged in, the pin senses the
externally held rail — around 4.8 V — rather than the cell. Two consequences:

- Charging is detected as "above any voltage a real cell can reach" (> 4400 mV).
- While charging, the level genuinely **cannot be read**. So the UI shows a filling
  battery animation instead of a number, rather than reporting a confident 100 %.

The voltage-to-charge mapping is a curve, not a line: a lithium cell says very
little about its charge across the flat 3.6–3.9 V plateau. The table in
`hal_battery.c` is a representative **loaded** discharge shape, deliberately not
fitted to one specific cell — tuning it to a single unit makes that unit slightly
better and every other one worse.

The reported percentage is then EMA-smoothed and passed through hysteresis that
moves one point at a time, only in the direction the physics allows. A discharging
gauge can therefore never tick back up, which is what caused an earlier 77↔78
flip-flop.

### Re-fitting the curve for a different cell

```bash
idf.py -C firmware -DLOTUS_BATTERY_CALIB=1 build flash
```

Run the device from full to empty **on battery** (no USB), then plug it back in and
watch the boot log. `hal_battery_log.c` samples the raw voltage to storage every
60 seconds — surviving a brownout — and prints the whole log as CSV on every boot:

```
BATTLOG_BEGIN n=271 interval_s=60 (0=boot marker)
BATTLOG 0 4074
BATTLOG 1 4061
...
BATTLOG_END
```

A `0` is a boot marker separating sessions; the run you want is the monotonically
falling segment just before one. Fit that to percentages and replace the `SOC` table.

Production builds compile the logger out entirely, so it never touches flash.

## Power

Tuned for battery life without changing any behaviour:

- **Interrupt-driven dial**, as above.
- **Dynamic frequency scaling** — `CONFIG_PM_ENABLE` plus tickless idle, with the
  CPU capped at 160 MHz and dropping to 80 MHz when idle. Plenty for this UI.
- **Light sleep is deliberately left off.** Enabling it changes how the display,
  touch and USB behave, and DFS already captures most of the saving on a workload
  this idle. Flip `light_sleep_enable` in `platform_main.c` if you accept the
  trade-off.
- **PWM backlight.** The panel backlight is by far the largest draw, so the
  brightness slider is the one control that meaningfully changes runtime.
- **Lazy timers.** LVGL only redraws dirty regions; the battery is polled at 900 ms
  and the game snapshot written at most every 2 seconds, and only when it changed.

Further savings, each a behaviour change and so opt-in: a screen-dim or screen-off
timeout with wake-on-input, and enabling automatic light sleep.

## Memory notes

Two configuration choices in `sdkconfig.defaults` exist because of specific failures:

**`CONFIG_LV_USE_CLIB_MALLOC=y`.** LVGL's built-in allocator is a fixed 64 KB pool
in internal RAM, and the UI outgrew it once remote play added screens. LVGL's malloc
assert spins forever on failure, so exhaustion showed up as a **main-task watchdog
reset**, not an out-of-memory error — which is a miserable thing to debug. Using
the C library heap instead lets allocations fall back to PSRAM.

**A custom partition table.** The default single-app layout gives the app 1 MB,
which this UI overflows once WiFi links in. `partitions.csv` defines two 3 MB
slots — roughly double the current image, which leaves room to grow and makes the
dial-to-dial transfer possible at all.
