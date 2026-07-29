# Two source groups, deliberately listed apart: everything under hal/ may touch
# the SDK and the board, and everything under app/ may not. tools/check-layers.sh
# enforces that; see ../../docs/ARCHITECTURE.md for why.

set(HAL_SRCS
    # ESP32-S3 implementation of petal_hal.h
    "hal/esp32s3/platform_main.c"
    "hal/esp32s3/hal_display.c"
    "hal/esp32s3/hal_touch.c"
    "hal/esp32s3/hal_dial.c"
    "hal/esp32s3/hal_battery.c"
    "hal/esp32s3/hal_battery_log.c"
    "hal/esp32s3/hal_storage.c"
    "hal/esp32s3/hal_radio.c"
    "hal/esp32s3/hal_ota.c"
    "hal/esp32s3/hal_system.c"
)

set(APP_SRCS
    "app/app_main.c"

    # model — the rules, with no idea what a pixel is
    "app/model/counter.c"
    "app/model/game.c"
    "app/model/persist.c"

    # net — the dial-to-dial protocols
    "app/net/net_link.c"
    "app/net/fw_push.c"

    # ui — the shared furniture
    "app/ui/ui_common.c"
    "app/ui/screen_manager.c"
    "app/ui/nav.c"
    "app/ui/lotus.c"

    # ui/screens — one file per screen
    "app/ui/screens/screen_life.c"
    "app/ui/screens/screen_counters.c"
    "app/ui/screens/screen_commander.c"
    "app/ui/screens/screen_tools.c"
    "app/ui/screens/screen_settings.c"
    "app/ui/screens/screen_setup.c"
    "app/ui/screens/screen_dice.c"
    "app/ui/screens/screen_calibrate.c"
    "app/ui/screens/screen_mode.c"
    "app/ui/screens/screen_pair.c"
    "app/ui/screens/screen_splash.c"
    "app/ui/screens/screen_update.c"
    "app/ui/screens/screen_order.c"
    "app/ui/screens/screen_result.c"
    "app/ui/screens/screen_about.c"
)

idf_component_register(
    SRCS ${HAL_SRCS} ${APP_SRCS}
    INCLUDE_DIRS
        "hal"
        "hal/esp32s3"
        "app"
        "app/model"
        "app/net"
        "app/ui"
        "app/ui/screens"
    REQUIRES
        # display + input + LVGL
        driver esp_lcd esp_lcd_st77916 esp_lcd_touch_cst816s esp_lvgl_port lvgl
        # power, timing, storage
        esp_timer esp_adc esp_pm nvs_flash
        # radio
        esp_wifi esp_event esp_netif
        # firmware images
        app_update esp_app_format esp_partition
)

# Battery-curve calibration build: idf.py -DLOTUS_BATTERY_CALIB=1 build
# Logs the discharge to storage and dumps it as CSV on boot. Off in production.
if(LOTUS_BATTERY_CALIB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LOTUS_BATTERY_CALIB=1)
endif()
