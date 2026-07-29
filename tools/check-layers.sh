#!/usr/bin/env sh
#
# check-layers.sh — fail if the application layer has reached past the HAL.
#
# The whole claim of this codebase is that app/ is portable and hal/ is not. That
# claim is only worth anything if something checks it, so: no file under app/ may
# include a platform SDK header, a FreeRTOS header, or a board header. The one
# door between the layers is petal_hal.h.
#
# Run it by hand, or from CI:
#     sh tools/check-layers.sh
#
# Exit status 0 = the boundary holds.

set -u

APP_DIR="firmware/main/app"

if [ ! -d "$APP_DIR" ]; then
    echo "check-layers: run me from the repository root" >&2
    exit 2
fi

# Includes that mean the file below has stopped being portable. petal_hal.h and
# petal_config.h are the contract itself, so they are not on the list.
FORBIDDEN='#[[:space:]]*include[[:space:]]*[<"](esp_|driver/|freertos/|nvs|hal/|soc/|board|sdkconfig|st77916)'

hits=$(grep -rnE "$FORBIDDEN" "$APP_DIR" --include='*.c' --include='*.h' 2>/dev/null || true)

if [ -n "$hits" ]; then
    echo "check-layers: FAIL — the application layer is reaching into the platform:" >&2
    echo "$hits" >&2
    echo >&2
    echo "Add what you need to firmware/main/hal/petal_hal.h and implement it in a" >&2
    echo "hal/<platform>/ file instead. See docs/ARCHITECTURE.md." >&2
    exit 1
fi

echo "check-layers: OK — $(find "$APP_DIR" -name '*.c' -o -name '*.h' | wc -l | tr -d ' ') application files, none reaching past petal_hal.h"
