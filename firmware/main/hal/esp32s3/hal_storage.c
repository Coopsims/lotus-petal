/*
 * hal_storage.c — key/value blobs in NVS.
 *
 * The app stores three things: the in-progress game, the touch calibration, and
 * the brightness. All of them are small fixed-layout structs, so the interface
 * is deliberately just "give me exactly these bytes back".
 *
 * The exact-length requirement in petal_kv_get() is load-bearing: it is how the
 * app rejects a blob written by an older build whose struct layout differed.
 * NVS also skips the physical write when a value has not changed, which is what
 * lets the app flush on a timer without wearing the flash out.
 */
#include "petal_hal.h"
#include "hal_internal.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "hal.store";

bool hal_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* A partition written by an incompatible NVS version, or one that filled
         * up. Nothing in here is precious enough to fail booting over. */
        ESP_LOGW(TAG, "nvs unusable (%s) — erasing", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool petal_kv_get(const char *ns, const char *key, void *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;

    size_t got = len;
    esp_err_t err = nvs_get_blob(h, key, out, &got);
    nvs_close(h);

    /* A different size means a different layout: refuse it rather than
     * reinterpreting someone else's bytes. */
    return err == ESP_OK && got == len;
}

bool petal_kv_set(const char *ns, const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;

    esp_err_t err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
