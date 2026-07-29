# Porting to different hardware

The requirement is a **dial**, a **touchscreen** and a **radio**. Everything else
is optional and degrades gracefully.

You will write one new implementation of
[`petal_hal.h`](../firmware/main/hal/petal_hal.h) and change nothing under `app/`.
If you find yourself editing a screen to make your board work, something has gone
wrong — the thing you need probably belongs in the contract instead.

## Before you start

Be honest about which of three situations you are in, because the work is very
different:

| Situation | Work |
|---|---|
| **Same SoC family, different pins.** Another ESP32-S3 module with the same panel and touch controller. | Copy the board header, change the pins. An hour. |
| **Same SoC family, different peripherals.** ESP32 of some kind, but a different panel or touch part. | Board header plus the display and touch files. An afternoon. |
| **Different SoC entirely.** STM32, nRF, RP2040 with a radio, a Linux SBC. | A new `hal/<platform>/` directory, ten files. A weekend, most of it on the display. |

In every case, the UI, the game rules, the persistence and both wire protocols come
along for free.

## Order of work

Do it in this order. Each step is verifiable on its own, and the display is both
the hardest part and the thing that lets you see the rest working.

### 1. Storage first

`petal_kv_get` / `petal_kv_set`. It is twenty lines over whatever non-volatile
store you have, and it comes first in `petal_hal_init()` because the display's
saved brightness and the touch map both live in it.

Honour the exact-length rule in `get()` — return `false` when the stored size does
not match. That is what stops a struct from an older build being misread.

### 2. Display

The one that takes real time. You need `petal_hal_init()` to leave you with LVGL
running on the panel, plus a working `petal_display()`.

- Get LVGL drawing *anything* before you worry about the app. A test pattern in
  the middle of the panel answers most questions at once.
- Byte order, colour inversion and element order are usually wrong at least once.
  The reference board needs all three of byte-swapped RGB565, inverted colour, and
  RGB element order — see [HARDWARE.md](HARDWARE.md#the-display-recipe).
- Set the panel size in **both** `boards/<yours>.h` and `petal_config.h`.
  `hal_display.c` static-asserts that they agree, so a mismatch is a build error
  rather than a subtly wrong image.
- If the glass is not round, set `PETAL_DISP_ROUND 0`. The screens will still keep
  their content in the middle; you gain corners you can use later.

### 3. Dial

`petal_dial_take()`. Read [the decode note](HARDWARE.md#the-dial-decode) first,
even if you are confident, because the failure mode is silent: a knob decoded as
textbook quadrature can report exactly zero counts and look like broken wiring.

Two sanity checks: one physical click must produce exactly ±1, and turning the knob
a full revolution slowly must produce the same count as turning it fast.

Prefer interrupts to polling. It is the single biggest CPU saving on a device that
is idle almost all the time.

### 4. Touch

`hal_touch_init()` plus the four calibration functions. Get raw coordinates
reaching LVGL first, then worry about accuracy — Settings → Calibrate Touch will
fix a bad map on-device, so `petal_touch_calib_defaults()` only has to be good
enough to hit that button.

`petal_touch_last_raw()` must report the sample **before** mapping. If it reports
the mapped value, the calibration screen fits the map against its own output and
converges on whatever it already had.

You now have a usable device: life totals, commander damage, counters, dice, save
and resume all work. Everything below is additive.

### 5. Battery, or not

If there is no battery sense, set `PETAL_HAS_BATTERY 0`, make
`petal_battery_present()` return false, and stub the rest. The gauge disappears.

If there is, the interesting part is not the ADC, it is
[the curve and the smoothing](HAL.md#battery). Start by copying the reference
implementation and adjusting the divider; re-fit the curve later with
`-DLOTUS_BATTERY_CALIB=1` if the numbers feel wrong.

### 6. Radio

Six functions, and the protocol above them stops caring what they sit on.

The constraints that bite are in [HAL.md](HAL.md#radio): a fixed channel, no modem
sleep, an honest MTU, and a stable address readable before the radio starts.

Test with two devices. One creates a game and shows a PIN, the other joins it; both
should show two seats within a second or two. If they never see each other, it is
almost always a channel mismatch or an association the radio is following.

### 7. Firmware transfer, or not

Needs two app slots and rollback. Without them, stub `petal_ota_begin()` to `NULL`
and `petal_ota_image_size()` to 0, and the transfer UI hides itself. Flashing over
a cable still works, which is all that is strictly required.

## What is *not* abstracted

Two honest limitations. Neither blocks a port; both may want work afterwards.

**Screen layout is in pixels, tuned for 360×360.** `UI_DIM` comes from
`PETAL_DISP_W`, and the pieces that could be derived have been — the calibration
targets, for instance, are computed from the panel size. But arc diameters, ring
radii, chip sizes and font choices are literals chosen for this face. On a
significantly different panel the app will build and run and look wrong. Grep for
`lv_obj_set_size`, `RING_R` and `lv_font_montserrat_` and expect to spend an
evening.

**The font sizes are compile-time.** They are enabled in `sdkconfig.defaults`
(`CONFIG_LV_FONT_MONTSERRAT_*`). A much larger or smaller panel needs different
ones enabled there as well as referenced in the screens.

## A minimal new platform

```
firmware/main/hal/
├── petal_config.h            edit: geometry + capability flags
├── boards/
│   ├── board.h               edit: point at your board header
│   └── board_myboard.h       new:  pins, panel size, ADC channel, fallback touch map
└── myplatform/               new:  your implementation
    ├── platform_main.c       entry point, power setup, bring-up order
    ├── hal_display.c         panel + LVGL + backlight
    ├── hal_touch.c           touch + the calibration map
    ├── hal_dial.c            encoder decode
    ├── hal_battery.c         ADC + curve, or stubs
    ├── hal_storage.c         key/value blobs
    ├── hal_radio.c           datagrams
    ├── hal_ota.c             image read/write, or stubs
    └── hal_system.c          time, random, reboot, identity, queues, tasks
```

Then swap the `HAL_SRCS` list and the `hal/esp32s3` include directory in
`firmware/main/CMakeLists.txt` for yours. `APP_SRCS` does not change — that is the
point.

## Checklist before you call it done

- [ ] `sh tools/check-layers.sh` passes.
- [ ] A clean build has no warnings.
- [ ] One dial detent moves life by exactly one, both directions.
- [ ] Tapping a target hits within a few pixels after calibrating.
- [ ] A swipe changes screen; a tap on the face opens commander damage. The two
      never trigger each other.
- [ ] Holding the middle of the face for ~0.7 s opens the Tools pie.
- [ ] Change life, pull the power, boot: the game comes back.
- [ ] Two devices pair by PIN and show each other's life on the rim.
- [ ] Brightness survives a reboot.
- [ ] Battery gauge either reads plausibly or is absent — not stuck at `--`.
- [ ] An image that crashes during bring-up gets rolled back rather than
      bricking the device. Worth testing deliberately once.
