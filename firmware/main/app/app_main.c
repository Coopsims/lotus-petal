/*
 * app_main.c — the application: what exists, what drives it, and what happens
 * at boot.
 *
 * Input model, which the whole UI is built around:
 *   turn the dial  -> change the value the current screen is about
 *   tap            -> select / press
 *   swipe sideways -> change screen
 * Taps never change a value, so a tap that was really the start of a swipe
 * cannot nudge someone's life total.
 *
 * Everything after boot runs from LVGL timers, all on the LVGL thread, so no
 * screen ever has to think about locking.
 */
#include "app.h"

#include "petal_hal.h"

#include "game.h"
#include "input_event.h"
#include "nav.h"
#include "net_link.h"
#include "fw_push.h"
#include "persist.h"
#include "screen_manager.h"

#include "screens/screen_about.h"
#include "screens/screen_calibrate.h"
#include "screens/screen_commander.h"
#include "screens/screen_counters.h"
#include "screens/screen_dice.h"
#include "screens/screen_life.h"
#include "screens/screen_mode.h"
#include "screens/screen_order.h"
#include "screens/screen_pair.h"
#include "screens/screen_result.h"
#include "screens/screen_settings.h"
#include "screens/screen_setup.h"
#include "screens/screen_splash.h"
#include "screens/screen_tools.h"
#include "screens/screen_update.h"

#include <stdlib.h>

/* Timer periods. The dial is polled fast enough to feel immediate; everything
 * else is as slow as it can be without being noticed. */
#define DIAL_PUMP_MS    20
#define NET_PUMP_MS     250
#define BATTERY_PUMP_MS 900     /* also the charging animation's frame rate */
#define PERSIST_PUMP_MS 2000

/* ---------- input pumps -------------------------------------------------- */

/* Turn accumulated detents into increment/decrement events. */
static void dial_pump(lv_timer_t *t)
{
    LV_UNUSED(t);

    int d = (int)petal_dial_take();
    if (!d) return;

    if (nav_active()) {
        nav_encoder(d);         /* an open overlay owns the dial */
        persist_mark_dirty();   /* commander damage etc. may have changed */
        return;
    }

    input_event_t ev = (d > 0) ? INPUT_EV_INCREMENT : INPUT_EV_DECREMENT;
    for (int i = 0, n = abs(d); i < n; i++) {
        screen_manager_handle_input(ev, NULL);
    }
    persist_mark_dirty();
}

/* Write the game snapshot when it has changed. */
static void persist_pump(lv_timer_t *t)
{
    LV_UNUSED(t);
    persist_flush();
}

/* Battery readout. While charging this runs the filling animation, which is why
 * it ticks well below once per second. */
static void battery_pump(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!petal_battery_present()) {
        screen_life_set_battery(-1, false);   /* hides the gauge */
        return;
    }
    bool charging = petal_battery_charging();
    screen_life_set_battery(charging ? 0 : petal_battery_percent(), charging);
}

/* Drives the link from the LVGL thread: drain received packets, republish our own
 * seat, resize the table to however many dials are linked, redraw the rim. All
 * no-ops until the player chooses a linked game. */
static void net_pump(lv_timer_t *t)
{
    LV_UNUSED(t);

    net_link_tick();
    game_sync_remote();

    /* An incoming firmware offer should be visible without the player having gone
     * looking for it. Testing whether the screen is up — rather than firing once
     * on the state change — means it also comes back if you were elsewhere at the
     * time, or backed out of it. The offer expires by itself, so this cannot trap
     * you on the screen. */
    fw_push_tick();
    fw_state_t fw = fw_push_state();
    if ((fw == FW_RECEIVING || fw == FW_OFFERED) && !screen_update_is_open()) {
        screen_update_open();
    }

    /* Surface the end of a round once, rather than every tick. */
    static bool saw_result;
    bool over = (game_result() != GAME_RESULT_NONE);
    if (over && !saw_result) screen_result_open();
    saw_result = over;

    screen_pair_tick();
    screen_counters_sync_shared();   /* monarch / initiative belong to the table */
    screen_life_refresh_peers();
}

/* ---------- touch: taps versus swipes ------------------------------------ */

/* Where the current press started. Both the swipe detector and the tap test
 * measure against it. */
static lv_point_t s_press_origin;

bool app_touch_release_was_tap(void)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return false;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dx = (int)p.x - (int)s_press_origin.x;
    int dy = (int)p.y - (int)s_press_origin.y;
    return abs(dx) <= PETAL_TAP_SLOP_PX && abs(dy) <= PETAL_TAP_SLOP_PX;
}

/* A horizontal swipe changes screen. We watch the touch device directly rather
 * than using LVGL's gesture detection, which is unreliable on the reference
 * panel. A swipe travels far enough that LVGL never reports CLICKED on the tap
 * zones, so taps and swipes do not compete. */
static void swipe_cb(lv_event_t *e)
{
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_press_origin = p;
        return;
    }
    if (code != LV_EVENT_RELEASED) return;

    int dx = (int)p.x - (int)s_press_origin.x;
    int dy = (int)p.y - (int)s_press_origin.y;
    if (abs(dx) <= PETAL_SWIPE_MIN_PX || abs(dx) <= abs(dy)) return;   /* not a swipe */

    if (nav_active()) {
        /* On an overlay, a right-swipe goes back one level; left means nothing. */
        if (dx > 0) nav_pop();
        return;
    }
    screen_manager_handle_input(dx < 0 ? INPUT_EV_NEXT_SCREEN
                                       : INPUT_EV_PREV_SCREEN, NULL);
}

/* ---------- boot --------------------------------------------------------- */

/* Runs once the splash clears. A game that was genuinely mid-flight is restored
 * silently — you land back at the table where you left it. A saved life of <= 0
 * (the game had ended) or still at the starting total (nothing had happened yet)
 * counts as no game, so we open the Local/Remote picker instead. */
static void boot_continue(void)
{
    int saved_life = persist_saved_life();
    if (persist_has_saved_game() && saved_life > 0 && saved_life != GAME_STARTING_LIFE) {
        persist_resume();
    } else {
        screen_mode_open();   /* Local or Remote; Local then asks for the pod size */
    }
}

void app_run(void)
{
    petal_lvgl_lock();

    /* Only two screens sit in the swipe cycle. Everything else is an overlay
     * pushed on top of them, so nothing important is reachable by accident. */
    screen_manager_add("Life", screen_life_create, screen_life_handle_input);
    screen_manager_add("Counters", screen_counters_create, screen_counters_handle_input);
    screen_manager_start();

    screen_commander_init();  /* commander damage (tap the Life face)   */
    screen_setup_init();      /* "how many players?"                    */
    screen_tools_init();      /* the hold-centre Tools pie              */
    screen_settings_init();   /* settings list                          */
    screen_calibrate_init();  /* touch calibration (from Settings)      */
    screen_mode_init();       /* opening Local / Remote choice          */
    screen_pair_init();       /* pairing by PIN                         */
    screen_splash_init();     /* boot splash                            */
    screen_update_init();     /* firmware transfer                      */
    screen_order_init();      /* seat picker, after the first-player roll */
    screen_result_init();     /* win / loss at the end of a round       */
    screen_about_init();      /* device info (from Settings)            */
    screen_dice_init();       /* dice and coin (from the Tools pie)     */

    fw_push_init();           /* accept an image pushed from a linked dial */

    game_set_player_count(game_player_count());   /* size the commander grid */
    persist_init();                               /* load any saved game */

    lv_indev_t *touch = petal_touch_indev();
    if (touch) {
        lv_indev_add_event_cb(touch, swipe_cb, LV_EVENT_PRESSED, touch);
        lv_indev_add_event_cb(touch, swipe_cb, LV_EVENT_RELEASED, touch);
        lv_indev_set_long_press_time(touch, PETAL_LONG_PRESS_MS);
    }

    lv_timer_create(dial_pump, DIAL_PUMP_MS, NULL);
    lv_timer_create(net_pump, NET_PUMP_MS, NULL);
    battery_pump(NULL);                                    /* seed it immediately */
    lv_timer_create(battery_pump, BATTERY_PUMP_MS, NULL);
    lv_timer_create(persist_pump, PERSIST_PUMP_MS, NULL);

    screen_splash_open(boot_continue);

    petal_lvgl_unlock();
}
