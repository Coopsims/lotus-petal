# Architecture

## The one idea

There are two halves, and one header between them.

```
        ┌──────────────────────────────────────────────────────┐
        │  app/            the counters, the screens, the      │
        │                  rules, the protocols               │
        │                  portable C + LVGL                  │
        └───────────────────────┬──────────────────────────────┘
                                │  petal_hal.h
        ┌───────────────────────┴──────────────────────────────┐
        │  hal/            pins, peripherals, vendor SDK       │
        │                  one implementation per platform     │
        └──────────────────────────────────────────────────────┘
```

Nothing in `app/` includes an SDK header, a FreeRTOS header, or a board header.
Nothing in `hal/` includes anything from `app/` — except the platform's entry
point, which calls `app_run()` once and never speaks to it again.

That is not a style preference, it is the product requirement: this firmware is
meant to run on *any* device with a dial, a touchscreen and a radio. If the UI
knows what an `esp_err_t` is, that stops being true the moment someone shows up
with different silicon.

`tools/check-layers.sh` greps `app/` for forbidden includes and fails if it finds
any. Run it before you push; it takes no time and it is the only thing standing
between the boundary and slow erosion.

## Why the boundary sits where it does

The interesting decisions were about what counts as hardware.

**The battery curve is hardware.** Turning millivolts into a percentage looks like
application logic, but the mapping is a property of the cell, and the smoothing
exists because *this* ADC is noisy. So the HAL answers `petal_battery_percent()`
and the UI just draws it. A board with a fuel gauge chip would answer the same
question by reading a register, and no screen would change.

**The touch calibration map is hardware; the calibration screen is not.** The map
has to be applied inside the touch driver, before LVGL ever sees a coordinate, so
it lives in the HAL. Fitting a new one is a UI flow with crosshairs and a
least-squares fit, so that lives in `app/ui/screens/screen_calibrate.c`. They meet
at four floats.

**The radio is hardware; both protocols are not.** `petal_hal.h` offers only
"broadcast these bytes", "send these bytes to that address" and "tell me when
bytes arrive". Everything above it — PIN-based membership, seat ordering, the
epoch that arbitrates shared state, stop-and-wait firmware transfer — is ordinary
C in `app/net/`, and would work unchanged over BLE advertising or UDP broadcast.

**Threading primitives are hardware.** The app is single-threaded by design, but
two things cannot be: the radio's receive hook, and the firmware transfer's
minutes-long transfer loop. Rather than let FreeRTOS leak upward, the HAL exposes a
queue and a task-start function. Those five functions are the whole of the app
layer's concurrency surface.

**Screen layout is not abstracted, and that is a real limitation.** The UI is
written in pixels for a round face. `petal_config.h` carries the panel size and
the app derives what it can from it, but the layout constants are tuned for
360×360. A different size builds and runs; it will not look right without
revisiting them. See [PORTING.md](PORTING.md).

## What is in each directory

### `hal/`

| Path | What it owns |
|---|---|
| `petal_hal.h` | The contract. Read this first; it documents the guarantees, not just the signatures. |
| `petal_config.h` | The handful of hardware facts the app may know: panel geometry, which optional devices exist, input feel constants. No pins, no SDK types. |
| `boards/board.h` | One line, selecting the active board header. |
| `boards/board_round360_knob.h` | Pins, panel geometry, ADC channel, the fallback touch map — and the notes on why the display recipe is what it is. |
| `boards/st77916_init.h` | The panel's init command table. |
| `esp32s3/platform_main.c` | `app_main()`, power management, and the bring-up order. |
| `esp32s3/hal_display.c` | QSPI panel, LVGL display, PWM backlight. |
| `esp32s3/hal_touch.c` | Touch controller, and the calibration map applied per sample. |
| `esp32s3/hal_dial.c` | Encoder interrupt and decode. |
| `esp32s3/hal_battery.c` | ADC, the charge curve, the smoothing. |
| `esp32s3/hal_battery_log.c` | Dev-only discharge logger for re-fitting that curve. |
| `esp32s3/hal_storage.c` | Key/value blobs in NVS. |
| `esp32s3/hal_radio.c` | ESP-NOW datagrams. |
| `esp32s3/hal_ota.c` | Reading the running image, writing the next one. |
| `esp32s3/hal_system.c` | Time, randomness, reboot, device identity, queues, tasks. |

### `app/`

| Path | What it owns |
|---|---|
| `app_main.c` | The application's entry point: registers screens, wires input, starts the timers, runs the boot flow. |
| `app.h` | `app_run()`, plus the tap-versus-swipe test the screens need. |
| `input_event.h` | The semantic input events. Screens never see coordinates or pins. |
| `model/counter.c` | One small struct that every countable thing in the app is expressed as. |
| `model/game.c` | Turn order, seating, elimination, win/loss, new game. The rules. |
| `model/persist.c` | Snapshot the game to storage; restore it at boot. |
| `net/net_link.c` | The hostless game link: membership, seats, shared round state. |
| `net/fw_push.c` | Dial-to-dial firmware transfer. |
| `ui/ui_common.c` | The palette and the shared widget helpers. |
| `ui/screen_manager.c` | The registry of swipe screens, and input dispatch. |
| `ui/nav.c` | The overlay stack that everything else is pushed onto. |
| `ui/lotus.c` | The mark, drawn from polylines rather than shipped as a bitmap. |
| `ui/screens/*.c` | One file per screen. See [APPLICATION.md](APPLICATION.md). |

## Threading

There are three contexts, and the split matters:

**The LVGL thread** runs every screen, every timer, and all of `model/`. Because
everything the UI touches lives here, no screen contains a lock. The platform owns
this thread; `petal_lvgl_lock()` / `petal_lvgl_unlock()` exist for the one moment
before it starts, when `app_run()` builds the UI from outside it.

**The radio's context** runs the receive hook. It is not allowed to touch a widget
and it is not allowed to block: it sorts the packet and pushes it onto a queue.
`net_link_tick()` drains that queue from an LVGL timer, so the member table is
only ever written from one thread and every screen can read it without
synchronising.

**Two firmware-transfer tasks** exist only while an image is moving. One drains the
transfer queue and writes flash; the other spends minutes in a stop-and-wait send
loop. Neither draws anything — they publish a state and a percentage that the
update screen polls.

Interrupt handlers (the dial) touch exactly one `volatile int32_t`, which the
input pump reads and decrements by the amount it read, so a detent landing between
those two operations is carried to the next tick rather than lost.

## Boot sequence

```
app_main()                      [platform: hal/esp32s3/platform_main.c]
 ├── power_init()               DFS: 160 MHz under load, 80 MHz idle
 └── petal_hal_init()
      ├── hal_storage_init()    FIRST: brightness and touch calibration live here
      ├── hal_display_init()    panel, then LVGL, then the backlight comes up
      ├── hal_touch_init()      needs the display to attach to
      ├── hal_dial_init()       AFTER touch: touch installs the shared GPIO ISR
      └── hal_battery_init()
 ├── hal_battery_log_init()     no-op unless built with -DLOTUS_BATTERY_CALIB
 ├── app_run()                  ── the boundary is crossed exactly here ──
 │    ├── register the two swipe screens, then build every overlay
 │    ├── persist_init()        load any saved game, without touching the UI
 │    ├── wire the touch device: swipe detection, long-press time
 │    ├── start four timers     dial 20 ms · net 250 ms · battery 900 ms · save 2 s
 │    └── screen_splash_open(boot_continue)
 │         └── boot_continue()  resume a real game, else ask Local or Remote
 └── petal_ota_mark_valid()     we have a working UI: cancel the rollback
```

That last line is worth understanding. The bootloader keeps two app slots and will
roll back to the previous one unless the new image says it booted successfully. It
only says so *after* the UI is up, so an image that crashes during bring-up gets
reverted automatically instead of stranding the device.

## The four timers

Everything after boot is driven by these. Nothing polls in a loop.

| Timer | Period | What it does |
|---|---|---|
| `dial_pump` | 20 ms | Drain accumulated detents; route them to the open overlay, or to the active screen as increment/decrement events. |
| `net_pump` | 250 ms | Drain radio packets, republish our seat, resize the table, surface an incoming firmware offer or the end of a round, redraw the rim. |
| `battery_pump` | 900 ms | Update the gauge. Doubles as the frame rate of the charging animation, which is why it is sub-second. |
| `persist_pump` | 2 s | Write the game snapshot, but only if something changed. |

## Data flow, from finger to pixel

```
dial detent  ─→ ISR (volatile counter)
             ─→ dial_pump  ─→ nav_encoder()            when an overlay is open
                            └→ screen_manager_handle_input(INCREMENT/DECREMENT)
                                └→ the active screen's handler
                                    └→ counter_increment()  ─→ redraw

touch press  ─→ LVGL indev  ─→ tap zones on the screen (CLICKED / LONG_PRESSED)
                            └→ swipe_cb: press→release displacement
                                ├→ overlay open + rightwards  ─→ nav_pop()
                                └→ otherwise                  ─→ next/prev screen

life changes ─→ persist_mark_dirty()  ─→ (2 s later) persist_flush()  ─→ storage
             ─→ net_link_publish()    ─→ broadcast  ─→ the other dials' rims
```

## Adding things

**A new counter or token.** Add one `counter_t` to the table in
`app/ui/screens/screen_counters.c`. The tiles, the input routing and the
persistence all size themselves from that table. Bump `PERSIST_VERSION` in
`app/model/persist.c` if you care about old saves not shifting.

**A new screen.** Write `screen_foo.c/h` following any existing one. If it belongs
in the swipe cycle, register it in `app_run()` with `screen_manager_add()`; if it
is reached from somewhere else — which is usually the right answer — have it push
itself with `nav_push()` instead. Add the file to `firmware/main/CMakeLists.txt`.

**A new piece of hardware.** Add the function to `petal_hal.h` with a comment
saying what an implementation must guarantee, implement it in `hal/esp32s3/`, and
give it a capability flag in `petal_config.h` if a board could plausibly lack it.
Then make the UI hide the feature when the flag is off, rather than showing a dead
control.
