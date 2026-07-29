/*
 * platform_main.c — the ESP-IDF entry point, and the only place that decides the
 * order hardware comes up in.
 *
 * app_main() belongs to the platform; the application's entry point is app_run(),
 * which knows nothing about any of this. Everything below is either an ordering
 * constraint or a power setting.
 */
#include "petal_hal.h"
#include "hal_internal.h"
#include "app.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"

static const char *TAG = "petal";

/* Bring-up order matters in three places:
 *   1. storage before anything that reads a saved setting (brightness, touch
 *      calibration), which is almost everything,
 *   2. the display before touch, since the touch device attaches to it,
 *   3. the dial after touch, because the touch driver installs the shared GPIO
 *      interrupt service and the dial only wants to add a handler to it. */
bool petal_hal_init(void)
{
    if (!hal_storage_init()) {
        ESP_LOGW(TAG, "storage unavailable — settings will not persist");
    }

    if (!hal_display_init()) {
        ESP_LOGE(TAG, "no display — nothing to run");
        return false;
    }

    if (!hal_touch_init(petal_display())) {
        ESP_LOGW(TAG, "no touch — dial-only control");
    }

    hal_dial_init();
    hal_battery_init();

    return true;
}

static void power_init(void)
{
#if CONFIG_PM_ENABLE
    /* Dynamic frequency scaling: up to 160 MHz under load, 80 MHz when idle.
     * Light sleep is left off deliberately — enabling it changes how the display,
     * touch and USB behave, and this UI is idle-heavy enough that DFS already
     * gets most of the saving. */
    esp_pm_config_t pm = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    esp_pm_configure(&pm);
#endif
}

void app_main(void)
{
    power_init();

    if (!petal_hal_init()) return;

    /* Only after the hardware is up, because the logger samples the ADC. A no-op
     * unless the build defines LOTUS_BATTERY_CALIB. */
    hal_battery_log_init();

    app_run();

    /* We got far enough to have a working UI, so keep this image: without this
     * the bootloader would roll back to the previous slot on the next boot. */
    petal_ota_mark_valid();

    ESP_LOGI(TAG, "up");
}
