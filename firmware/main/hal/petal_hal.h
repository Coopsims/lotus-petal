/*
 * petal_hal.h — the hardware contract. This file is the boundary between the
 * two halves of Lotus Petal.
 *
 *   app/  the counter, the screens, the game rules, the link protocol.
 *         Portable C + LVGL. Includes this header and nothing else from below.
 *   hal/  every line that touches a pin, a peripheral or a vendor SDK.
 *         Implements this header. Includes nothing from above.
 *
 * A port to different hardware means writing a new implementation of the
 * functions below and leaving app/ completely alone. hal/esp32s3/ is the
 * reference implementation (ESP32-S3 + ESP-IDF); docs/PORTING.md walks through
 * doing another one, and tools/check-layers.sh fails the build if app/ ever
 * reaches past this header.
 *
 * Contract notes that apply throughout:
 *   - Everything here is called from the LVGL thread unless a function says
 *     otherwise, so no implementation needs internal locking for app callers.
 *   - Optional hardware (see petal_config.h) still has to provide its functions;
 *     they may be stubs. Ask petal_battery_present() rather than guessing.
 *   - Nothing here blocks for more than a few milliseconds except where noted.
 */
#ifndef PETAL_HAL_H
#define PETAL_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "petal_config.h"

/* =========================================================================
 * Bring-up
 * ========================================================================= */

/**
 * Bring up every device the application needs, in the order the hardware
 * requires, and start LVGL on top of the panel.
 *
 * Called once, before app_run(). Returns false only when the display could not
 * be started — without a screen there is nothing worth continuing for. Missing
 * optional hardware is reported through the per-device queries instead.
 */
bool petal_hal_init(void);

/** The LVGL display driving the panel. Valid after a successful petal_hal_init(). */
lv_display_t *petal_display(void);

/** The LVGL pointer device for the touch panel, or NULL if touch failed to
 *  start. The app degrades to dial-only control rather than refusing to run. */
lv_indev_t *petal_touch_indev(void);

/** Take the LVGL lock. The platform runs LVGL in its own task, so anything
 *  touching widgets from outside an LVGL callback must hold this. */
void petal_lvgl_lock(void);
void petal_lvgl_unlock(void);

/* =========================================================================
 * Dial (rotary encoder)
 * ========================================================================= */

/**
 * Detents accumulated since the last call: positive clockwise, negative
 * counter-clockwise, zero when the knob has not moved. Reading clears the
 * count, so exactly one caller should poll it (the app's input pump does).
 *
 * One unit is one physical detent. A dial cannot report finer than its own
 * mechanical steps, so implementations must not interpolate — but they must not
 * drop detents either, which on some knobs is harder than it sounds
 * (see docs/HARDWARE.md).
 */
int32_t petal_dial_take(void);

/* =========================================================================
 * Backlight
 * ========================================================================= */

/** Set brightness 5..100 % (clamped; never fully dark, which would look like a
 *  dead device). Takes effect immediately and is not persisted. */
void petal_backlight_set(int pct);

/** Current brightness percentage. */
int petal_backlight_get(void);

/** Persist the current brightness so it survives a power cycle. Called once
 *  when the slider is released rather than on every step, to spare the flash. */
void petal_backlight_save(void);

/* =========================================================================
 * Battery
 * ========================================================================= */

/** False when the board has no battery sense wired; the gauge is then hidden. */
bool petal_battery_present(void);

/** Cell voltage in millivolts, averaged over a few samples. 0 when unavailable. */
int petal_battery_millivolts(void);

/** Charge estimate 0..100 %, smoothed so the digit does not flicker. The
 *  voltage->charge mapping is a property of the cell, so it belongs to the
 *  board port, not to the UI. */
int petal_battery_percent(void);

/** True when running on external power. Boards without a charge-status line
 *  infer this from the sensed rail sitting above any real cell voltage — in
 *  which case petal_battery_percent() is meaningless while it is true, and the
 *  UI shows a charging animation instead of a number. */
bool petal_battery_charging(void);

/* =========================================================================
 * Touch calibration
 * ========================================================================= */

/* Raw panel coordinates rarely land exactly on display pixels, and the error
 * differs from unit to unit. The HAL applies a per-axis linear map inside the
 * touch driver; the app owns the calibration *screen* and drives it through
 * these four coefficients. Layout: [0] x scale, [1] x offset,
 * [2] y scale, [3] y offset — display = scale * raw + offset. */
#define PETAL_TOUCH_CALIB_N 4

/** The board's built-in fallback map, used until a unit is calibrated. */
void petal_touch_calib_defaults(float coef[PETAL_TOUCH_CALIB_N]);

/** The map currently in force. */
void petal_touch_calib_get(float coef[PETAL_TOUCH_CALIB_N]);

/** Make `coef` the active map and persist it. False on a storage error. */
bool petal_touch_calib_set(const float coef[PETAL_TOUCH_CALIB_N]);

/** The most recent RAW sample the panel reported, before mapping. This is what
 *  the calibration screen fits against. */
void petal_touch_last_raw(uint16_t *raw_x, uint16_t *raw_y);

/* =========================================================================
 * Key/value storage
 * ========================================================================= */

/* Small named blobs that survive a power cycle: the saved game, the touch map,
 * the brightness. Namespaced so unrelated data cannot collide. Writes are
 * expected to be cheap when the value has not actually changed — the app saves
 * on a timer and relies on that. */

/** Read `len` bytes into `out`. False when absent, or stored at another size
 *  (which is how the app rejects a snapshot written by an older layout). */
bool petal_kv_get(const char *ns, const char *key, void *out, size_t len);

/** Write `len` bytes and commit. False on a storage error. */
bool petal_kv_set(const char *ns, const char *key, const void *data, size_t len);

/* =========================================================================
 * Radio
 * ========================================================================= */

/* Connectionless datagrams between dials in radio range — no access point, no
 * association, no addressing beyond the peer's own hardware address. ESP-NOW on
 * the reference board; anything with the same shape (a raw 802.11 frame, BLE
 * advertising, even UDP broadcast on a shared network) can stand in.
 *
 * The link protocol in app/net is built only on what is below, and assumes
 * nothing about delivery: packets may be lost, duplicated or reordered. */

/** Largest payload a single datagram can carry, in bytes. The firmware
 *  transfer sizes its chunks from this, so it must be honest. */
int petal_radio_mtu(void);

/** Power the radio up. Safe to call repeatedly; false if it could not start.
 *  Only called once the player chooses a linked game, so a local game never
 *  powers the radio at all. */
bool petal_radio_init(void);

/** Release the radio entirely. A later petal_radio_init() rebuilds it. */
void petal_radio_shutdown(void);

/** True while the radio is up. */
bool petal_radio_is_up(void);

/** This device's own 6-byte hardware address. Stable across reboots, unique per
 *  unit, and readable whether or not the radio is up — the protocol uses it as
 *  the device identity and to order seats, so it must be all three. */
void petal_radio_self_mac(uint8_t out[6]);

/**
 * Receive hook for every inbound datagram.
 *
 * Called from whatever context the radio driver uses — NOT the LVGL thread. It
 * must not touch widgets and must not block: queue the bytes and return. `data`
 * is only valid for the duration of the call.
 */
typedef void (*petal_radio_rx_fn)(const uint8_t src_mac[6], const uint8_t *data, int len);

/** Install the receive hook (one at a time; the app fans out below it). */
void petal_radio_set_rx(petal_radio_rx_fn fn);

/** Send to every dial in range. */
bool petal_radio_broadcast(const void *data, int len);

/** Send to one dial. Implementations add the peer on demand, so the caller
 *  never manages a peer table. */
bool petal_radio_send(const uint8_t mac[6], const void *data, int len);

/* =========================================================================
 * Firmware images
 * ========================================================================= */

/* Enough of an updater for one dial to hand its own running image to another:
 * read the image you are running, write a new one into the spare slot, boot it.
 * A platform with a single app slot can stub all of this out — the app hides
 * the transfer UI when petal_ota_image_size() returns 0. */

typedef struct petal_ota petal_ota_t;   /* opaque write handle */

/** Size in bytes of the image this device is running, or 0 if it cannot be
 *  determined (in which case sending is unavailable). Must count only the
 *  image, not the whole partition — the difference is megabytes of erased
 *  flash nobody wants to transmit one 236-byte chunk at a time. */
size_t petal_ota_image_size(void);

/** Read from the running image. Offsets are within petal_ota_image_size(). */
bool petal_ota_image_read(size_t offset, void *buf, size_t len);

/** Open the inactive slot for writing. NULL if there is nowhere to put an image. */
petal_ota_t *petal_ota_begin(void);

/** Append to the slot. False on a write error (the caller then aborts). */
bool petal_ota_write(petal_ota_t *h, const void *data, size_t len);

/** Verify the written image and mark it to boot next. False if it does not
 *  verify — a corrupt transfer must never become the boot image. Consumes `h`. */
bool petal_ota_finish(petal_ota_t *h);

/** Give up on a partial write. Consumes `h`. */
void petal_ota_abort(petal_ota_t *h);

/** Confirm the running image works, cancelling any pending rollback. Called
 *  once the UI is up: a build that cannot reach this point gets rolled back by
 *  the bootloader instead of stranding the device. */
void petal_ota_mark_valid(void);

/* =========================================================================
 * System
 * ========================================================================= */

/** Monotonic microseconds since boot. Never goes backwards. */
int64_t petal_now_us(void);

/** Block the calling task. Not for use from LVGL callbacks. */
void petal_delay_ms(uint32_t ms);

/** Restart the device. Does not return. */
void petal_reboot(void);

/** A uniformly distributed random 32-bit value. Dice rolls and pairing PINs
 *  come from here, so it must not be a predictable sequence — seed it from a
 *  hardware source if the platform has one. */
uint32_t petal_random(void);

/** Identity for the About screen. Every string is NUL-terminated; a platform
 *  that cannot answer one leaves it empty rather than inventing a value. */
typedef struct {
    uint8_t mac[6];        /* same address petal_radio_self_mac() reports */
    char    chip[24];      /* "ESP32-S3 rev0"          */
    char    build_date[24];/* when this image was built */
    char    sdk[24];       /* platform SDK version      */
} petal_device_info_t;

void petal_device_info(petal_device_info_t *out);

/* =========================================================================
 * Tasks and queues
 * ========================================================================= */

/* The application is single-threaded by design: everything runs on the LVGL
 * thread. Two things cannot be — the radio receive hook (which runs wherever
 * the driver puts it) and the firmware transfer (which spends minutes in a
 * stop-and-wait loop). Both hand work across threads with the primitives below,
 * which is the whole of the app layer's concurrency surface. */

typedef struct petal_queue petal_queue_t;

/** Fixed-size ring of `depth` items of `item_size` bytes. NULL on failure. */
petal_queue_t *petal_queue_create(int depth, size_t item_size);

/** Copy one item in. Never blocks: returns false if the queue is full, which
 *  callers treat as "drop it" — the protocol re-sends. Safe from any context,
 *  including an interrupt. */
bool petal_queue_send(petal_queue_t *q, const void *item);

/** Copy one item out, waiting up to `timeout_ms` (0 = poll, -1 = forever).
 *  False on timeout. */
bool petal_queue_recv(petal_queue_t *q, void *item, int timeout_ms);

/** Discard everything queued. */
void petal_queue_reset(petal_queue_t *q);

/** Start a thread running `fn(arg)`. `stack_bytes` is a hint on platforms that
 *  need one. The thread runs until `fn` returns. False if it could not start. */
bool petal_task_start(void (*fn)(void *arg), void *arg, const char *name,
                      int stack_bytes, int priority);

/** Ends the calling task started by petal_task_start(). Does not return. */
void petal_task_exit(void);

#endif /* PETAL_HAL_H */
