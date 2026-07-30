/*
 * hal_system.c — time, randomness, reboot, device identity, tasks and queues.
 *
 * The concurrency primitives here are thin wrappers over FreeRTOS. They exist so
 * the app layer can hand work between threads — the radio receive hook and the
 * firmware transfer both need to — without including a platform header. That
 * keeps the layer boundary checkable instead of merely intended.
 */
#include "petal_hal.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- time and randomness ------------------------------------------ */

int64_t petal_now_us(void)
{
    return esp_timer_get_time();
}

void petal_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void petal_reboot(void)
{
    esp_restart();
}

uint32_t petal_random(void)
{
    /* Hardware RNG. Dice rolls and pairing PINs come through here, so a
     * deterministic PRNG would be the wrong answer. */
    return esp_random();
}

/* ---------- identity ----------------------------------------------------- */

void petal_device_info(petal_device_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    esp_read_mac(out->mac, ESP_MAC_WIFI_STA);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char *model = "ESP32";
    switch (chip.model) {
    case CHIP_ESP32S3: model = "ESP32-S3"; break;
    case CHIP_ESP32S2: model = "ESP32-S2"; break;
    case CHIP_ESP32C3: model = "ESP32-C3"; break;
    default: break;
    }
    snprintf(out->chip, sizeof(out->chip), "%s rev%d", model, chip.revision / 100);

    /* Straight out of the app descriptor, which CMake fills from PROJECT_VER. */
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(out->app_version, sizeof(out->app_version), "%s", app ? app->version : "");
    snprintf(out->build_date, sizeof(out->build_date), "%s", app ? app->date : "");
    snprintf(out->sdk, sizeof(out->sdk), "%s", IDF_VER);
}

/* ---------- queues ------------------------------------------------------- */

/* petal_queue_t is opaque to callers; on this platform it is just the FreeRTOS
 * handle, cast rather than wrapped so a queue costs nothing extra. */

petal_queue_t *petal_queue_create(int depth, size_t item_size)
{
    if (depth <= 0 || item_size == 0) return NULL;
    return (petal_queue_t *)xQueueCreate((UBaseType_t)depth, item_size);
}

bool petal_queue_send(petal_queue_t *q, const void *item)
{
    if (!q) return false;
    /* Never blocks: a full queue means the caller should drop the packet, and
     * this may be called from an interrupt. */
    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        BaseType_t ok = xQueueSendFromISR((QueueHandle_t)q, item, &woken);
        if (woken) portYIELD_FROM_ISR();
        return ok == pdTRUE;
    }
    return xQueueSend((QueueHandle_t)q, item, 0) == pdTRUE;
}

bool petal_queue_recv(petal_queue_t *q, void *item, int timeout_ms)
{
    if (!q) return false;
    TickType_t wait = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive((QueueHandle_t)q, item, wait) == pdTRUE;
}

void petal_queue_reset(petal_queue_t *q)
{
    if (q) xQueueReset((QueueHandle_t)q);
}

/* ---------- tasks -------------------------------------------------------- */

typedef struct {
    void (*fn)(void *arg);
    void *arg;
} task_entry_t;

/* Adapts a plain "runs and returns" function to FreeRTOS, where a task must
 * delete itself rather than fall off the end. */
static void task_trampoline(void *param)
{
    task_entry_t entry = *(task_entry_t *)param;
    free(param);
    entry.fn(entry.arg);
    vTaskDelete(NULL);
}

bool petal_task_start(void (*fn)(void *arg), void *arg, const char *name,
                      int stack_bytes, int priority)
{
    if (!fn) return false;
    task_entry_t *entry = malloc(sizeof(*entry));
    if (!entry) return false;
    entry->fn  = fn;
    entry->arg = arg;

    if (xTaskCreate(task_trampoline, name ? name : "petal", (uint32_t)stack_bytes,
                    entry, (UBaseType_t)priority, NULL) != pdPASS) {
        free(entry);
        return false;
    }
    return true;
}

void petal_task_exit(void)
{
    vTaskDelete(NULL);
}
