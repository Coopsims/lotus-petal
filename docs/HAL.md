# The hardware contract

Everything the application can ask of the hardware is in
[`firmware/main/hal/petal_hal.h`](../firmware/main/hal/petal_hal.h), and this page
explains what an implementation actually has to guarantee for each part of it.
The header is the normative version — if the two ever disagree, the header wins.

The reference implementation is `hal/esp32s3/`, one file per device. Read it
alongside this.

## Ground rules

- **Called from the LVGL thread** unless a function says otherwise. No
  implementation needs internal locking for app-side callers.
- **Nothing blocks** for more than a few milliseconds, except `petal_delay_ms()`
  and `petal_queue_recv()` with a timeout — neither of which is called from a
  screen.
- **Optional hardware still needs its functions.** They may be stubs. The app asks
  `petal_battery_present()` rather than guessing, and hides UI accordingly.
- **Say no clearly.** A function that cannot do its job returns `false`, `0` or
  `NULL`. The app is written to degrade rather than to assume: no touch means
  dial-only control; no spare app slot means the firmware transfer never offers
  itself.

## Bring-up

```c
bool petal_hal_init(void);
lv_display_t *petal_display(void);
lv_indev_t   *petal_touch_indev(void);
void petal_lvgl_lock(void);
void petal_lvgl_unlock(void);
```

`petal_hal_init()` brings up every device in whatever order the hardware demands,
and starts LVGL on the panel. Return `false` only if the display failed — there is
nothing worth continuing for without a screen. Everything else that failed is
reported through its own query.

By the time it returns, LVGL must be initialised, ticking, and running the panel.
On ESP-IDF that is `esp_lvgl_port`, which owns an LVGL task; `petal_lvgl_lock()`
guards it. If your platform runs LVGL on the only thread there is, the lock can be
a no-op — but it must still exist, because `app_run()` calls it.

`petal_touch_indev()` may return `NULL`. The app checks, and simply does not
install swipe handling. Every screen stays operable with the dial alone.

## Dial

```c
int32_t petal_dial_take(void);
```

Detents since the last call, positive clockwise. Reading clears the count, so
exactly one caller may poll it — the app's input pump does, every 20 ms.

Two requirements that are easy to get wrong:

**One unit is one physical detent.** Do not interpolate, and do not scale. The UI
maps one detent to one point of life, and a knob reporting four counts per click is
unusable.

**Do not drop detents.** This is the hard part, and it is hardware-specific: the
reference knob is not a textbook quadrature encoder, and decoding it as one nets
zero counts per detent and reports *nothing at all*. See
[HARDWARE.md](HARDWARE.md#the-dial-decode) before you write your own.

The reference implementation is interrupt-driven rather than polled, which matters
for battery life: a 5 kHz poll of two pins that change a few times a second is
thousands of pointless wakeups a second.

## Backlight

```c
void petal_backlight_set(int pct);   /* 5..100, clamped */
int  petal_backlight_get(void);
void petal_backlight_save(void);
```

Clamp to a 5 % floor. Fully dark reads as a dead device, and someone who dimmed
too far with the slider needs to be able to see it to drag it back.

`set` is live and not persisted; `save` writes the current value. The Settings
slider calls `set` on every step and `save` once on release, which is the
difference between one flash write and a hundred.

On a board with no dimmable backlight, guard the implementation with
`PETAL_HAS_BACKLIGHT` — `set` becomes a no-op and `get` can return 100.

## Battery

```c
bool petal_battery_present(void);
int  petal_battery_millivolts(void);
int  petal_battery_percent(void);
bool petal_battery_charging(void);
```

`present()` is false when there is no battery sense wired, and the app hides the
gauge rather than showing a permanent `--`.

`percent()` owes the caller a **stable** number, not a fresh one. A raw curve
lookup wanders by a point or two between samples, and a gauge that flickers
between 77 and 78 is worse than one that lags. The reference implementation
smooths with an EMA and then reports through hysteresis that only moves one point
at a time, and only in the direction the physics allows — a discharging gauge can
never tick up. A large divergence (plugged in, unplugged) snaps immediately.

`charging()` on a board with no charge-status line is inferred: a reading above
any voltage a real cell can reach must be an externally held rail. When it is
true, `percent()` is meaningless — it is measuring the charger, not the cell — and
the UI shows a filling animation instead of a number. If your board *does* have a
charge line, read it, and the same UI still works.

The voltage-to-charge curve belongs to the cell, so it belongs to the board port.
See [HARDWARE.md](HARDWARE.md#battery) for re-fitting it.

## Touch calibration

```c
#define PETAL_TOUCH_CALIB_N 4   /* [x scale, x offset, y scale, y offset] */

void petal_touch_calib_defaults(float coef[PETAL_TOUCH_CALIB_N]);
void petal_touch_calib_get(float coef[PETAL_TOUCH_CALIB_N]);
bool petal_touch_calib_set(const float coef[PETAL_TOUCH_CALIB_N]);
void petal_touch_last_raw(uint16_t *raw_x, uint16_t *raw_y);
```

The map is `display = scale × raw + offset`, per axis, applied inside the touch
driver before LVGL sees anything.

`petal_touch_last_raw()` must report the most recent **raw** sample, before
mapping. That is what the calibration screen fits against, and returning a mapped
value would make calibration converge on whatever the current map already is.

`set()` persists and takes effect immediately. `defaults()` is the board's
hand-measured fallback, which only has to be good enough to reach Settings →
Calibrate Touch.

A panel that genuinely needs no correction can make the map identity and stub
`last_raw()`; the calibration screen will then save a near-identity fit, which is
harmless.

## Storage

```c
bool petal_kv_get(const char *ns, const char *key, void *out, size_t len);
bool petal_kv_set(const char *ns, const char *key, const void *data, size_t len);
```

Small named blobs that survive a power cycle. Three exist: the saved game, the
touch map, the brightness.

**The exact-length requirement is load-bearing.** `get` must return `false` if the
stored value is a different size from `len`. That is one of the two mechanisms
stopping a build from misreading a struct written by an older layout — the other
being the magic-and-version pair inside the blob itself.

**A write of an unchanged value should be cheap.** The app flushes the game
snapshot on a 2-second timer and leans on this; if your backend physically erases
a sector every time, add the comparison yourself.

## Radio

```c
int  petal_radio_mtu(void);
bool petal_radio_init(void);
void petal_radio_shutdown(void);
bool petal_radio_is_up(void);
void petal_radio_self_mac(uint8_t out[6]);
void petal_radio_set_rx(petal_radio_rx_fn fn);
bool petal_radio_broadcast(const void *data, int len);
bool petal_radio_send(const uint8_t mac[6], const void *data, int len);
```

Connectionless datagrams to dials in range. No access point, no association, no
addressing beyond the peer's own hardware address. ESP-NOW on the reference board;
raw 802.11, BLE advertising, or UDP broadcast on a shared network would all fit.

What the protocol above assumes, and nothing more:

**`mtu()` must be honest.** Both protocols size their packets from it, and
`fw_push_init()` disables the firmware transfer outright rather than truncating if
it does not fit. ESP-NOW answers 250.

**The address must be stable, unique, and readable before the radio starts.** It is
the device identity *and* the seat ordering: every dial sorts all members'
addresses to derive the same seat order with no negotiation. The reference reads it
from eFuse, so it answers whether or not the radio is powered.

**The receive hook runs in the driver's context, not the LVGL thread.** It must not
touch a widget and must not block. `data` is only valid for the duration of the
call. One hook at a time; the app fans out beneath it.

**Delivery is not guaranteed and does not need to be.** Packets may be lost,
duplicated or reordered. The game link corrects itself on the next heartbeat; the
firmware transfer acknowledges every chunk.

**Two things a replacement must reproduce:** a fixed channel (broadcast only
reaches peers listening on the same one, and an associated station follows its
access point's channel instead — which is why the reference drops any association
on purpose), and no modem sleep (a sleeping radio misses the other dials).

`petal_radio_send()` adds the peer on demand. Callers never manage a peer table.

## Firmware images

```c
size_t       petal_ota_image_size(void);
bool         petal_ota_image_read(size_t offset, void *buf, size_t len);
petal_ota_t *petal_ota_begin(void);
bool         petal_ota_write(petal_ota_t *h, const void *data, size_t len);
bool         petal_ota_finish(petal_ota_t *h);
void         petal_ota_abort(petal_ota_t *h);
void         petal_ota_mark_valid(void);
```

Just enough for one dial to hand its running image to another.

**`image_size()` must be the image, not the partition.** The difference is
megabytes of erased flash that would otherwise be transmitted 236 bytes at a time.
The reference walks the image's segment headers to find the real end. Return 0 if
you cannot tell, and the app hides the transfer UI.

**`finish()` must verify before it commits.** A transfer that lost bytes must never
become the boot image. Returning `false` here is how a corrupt image gets rejected
instead of installed.

**`mark_valid()` is the anti-brick.** With two slots and rollback enabled, an image
that never calls this is reverted by the bootloader on the next boot. The app calls
it only after the UI is up, so "it booted" means something.

A single-slot platform can stub all of this: `begin()` returns `NULL`,
`image_size()` returns 0, and the feature disappears from the UI.

## System

```c
int64_t  petal_now_us(void);
void     petal_delay_ms(uint32_t ms);
void     petal_reboot(void);
uint32_t petal_random(void);
void     petal_device_info(petal_device_info_t *out);
```

`now_us()` is monotonic and never goes backwards — every timeout in the app is a
subtraction of two of these.

`random()` must not be a predictable sequence. It rolls the dice and generates
pairing PINs; a deterministic PRNG seeded the same way on every unit would make
every dial roll identically, which is exactly wrong for deciding who goes first.
Use the platform's hardware RNG.

`device_info()` fills the About page. Leave a field empty rather than inventing a
value; the screen omits empty rows.

## Tasks and queues

```c
petal_queue_t *petal_queue_create(int depth, size_t item_size);
bool petal_queue_send(petal_queue_t *q, const void *item);
bool petal_queue_recv(petal_queue_t *q, void *item, int timeout_ms);
void petal_queue_reset(petal_queue_t *q);
bool petal_task_start(void (*fn)(void *), void *arg, const char *name,
                      int stack_bytes, int priority);
void petal_task_exit(void);
```

These exist so the app can hand work between threads without including a platform
header. They are the entire concurrency surface above the boundary.

**`send()` never blocks.** A full queue returns `false`, which callers treat as
"drop it" — the game link's next heartbeat re-syncs. It must also be safe from an
interrupt; the reference checks the context and uses the ISR-safe variant.

**`recv(timeout_ms)`** takes 0 to poll and -1 to wait forever.

**`task_start()`'s function runs and returns.** The implementation is responsible
for whatever cleanup the platform needs — on FreeRTOS a task must delete itself, so
the reference wraps `fn` in a trampoline that does. `stack_bytes` is a hint;
ignore it on a platform that does not need one.
