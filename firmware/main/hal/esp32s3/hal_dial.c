/*
 * hal_dial.c — rotary dial decode.
 *
 * The knob on the reference board is NOT a textbook quadrature encoder, and
 * decoding it as one silently loses every click. Reverse-engineered from a raw
 * GPIO capture of one slow rotation:
 *
 *   rest: A=1 B=1
 *   A=0 B=1   <- A departs: the PRIMARY pulse, dwelling low for tens of ms
 *   A=1 B=1   <- A back at rest
 *   A=1 B=0   <- B blips low ~0.2 ms later: a SECONDARY, ~0.2 ms wide
 *   A=1 B=1   <- B back at rest
 *             <- long quiet gap, then the next detent
 *
 * Counter-clockwise is the mirror image (primary on B, secondary on A). So the
 * usual "A-fall = +1, B-fall = -1" accumulator scores +1 then -1 for every
 * detent, nets zero, and reports nothing at all — and no amount of time-based
 * debouncing rescues it, because the two edges are ~34 ms apart.
 *
 * What works: decide direction by which line leaves rest FIRST, and accept the
 * count only if the knob had been at rest for at least ENC_REARM_US just before
 * departing. That single gate swallows both the 0.2 ms secondary blip and the
 * ~0.6 ms contact-bounce re-fires, while real detents — milliseconds apart —
 * always qualify.
 *
 * Edge-triggered rather than polled: the CPU idles between detents instead of
 * waking thousands of times a second to read two pins that rarely change.
 */
#include "petal_hal.h"
#include "hal_internal.h"
#include "boards/board.h"

#include "driver/gpio.h"
#include "esp_timer.h"

/* Minimum rest before a departure counts, in microseconds. */
#define ENC_REARM_US 1200

static volatile int32_t s_delta;
static int     s_at_rest = 1;
static int64_t s_rest_enter_us;

static void IRAM_ATTR enc_isr(void *arg)
{
    (void)arg;
    int a = gpio_get_level(BOARD_ENC_PIN_A);
    int b = gpio_get_level(BOARD_ENC_PIN_B);
    int64_t now = esp_timer_get_time();

    if (a == 1 && b == 1) {                 /* entered rest */
        if (!s_at_rest) {
            s_at_rest = 1;
            s_rest_enter_us = now;
        }
    } else if (s_at_rest) {                 /* just left rest */
        s_at_rest = 0;
        if (now - s_rest_enter_us >= ENC_REARM_US) {
            if (a == 0 && b == 1)      s_delta++;   /* A left first = clockwise */
            else if (a == 1 && b == 0) s_delta--;   /* B left first = the other way */
        }
    }
}

void hal_dial_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_ENC_PIN_A) | (1ULL << BOARD_ENC_PIN_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    s_at_rest = (gpio_get_level(BOARD_ENC_PIN_A) == 1 &&
                 gpio_get_level(BOARD_ENC_PIN_B) == 1);
    s_rest_enter_us = esp_timer_get_time();

    /* The touch driver may already own the shared GPIO ISR service, which is
     * fine — we only want a handler on it. */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(gpio_isr_handler_add(BOARD_ENC_PIN_A, enc_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BOARD_ENC_PIN_B, enc_isr, NULL));
}

int32_t petal_dial_take(void)
{
    /* Read once and subtract exactly what we read, so a detent landing between
     * the two statements is carried to the next call instead of being lost. */
    int32_t d = s_delta;
    if (d) s_delta -= d;
    return d;
}
