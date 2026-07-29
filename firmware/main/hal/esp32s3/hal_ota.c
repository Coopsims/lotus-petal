/*
 * hal_ota.c — reading the running firmware image, and writing the next one.
 *
 * The partition table (../../partitions.csv) gives the app two slots. A dial
 * reads its own running image out of one and writes it into the other, then
 * boots it; the bootloader rolls back if the new image never confirms itself,
 * so a bad transfer cannot strand a device.
 */
#include "petal_hal.h"

#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "hal.ota";

struct petal_ota {
    esp_ota_handle_t       handle;
    const esp_partition_t *slot;
};

/* Only one transfer runs at a time, so the handle can live here rather than
 * being heap-allocated. */
static struct petal_ota s_writer;
static bool             s_writer_busy;

/* Walk the image's segment headers to find where it actually ends. The
 * alternative — shipping the whole partition — would mean transmitting megabytes
 * of erased flash one 236-byte chunk at a time. */
static size_t image_size(const esp_partition_t *part)
{
    esp_image_header_t hdr;
    if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK) return 0;
    if (hdr.magic != ESP_IMAGE_HEADER_MAGIC) return 0;

    size_t off = sizeof(esp_image_header_t);
    for (int i = 0; i < hdr.segment_count; i++) {
        esp_image_segment_header_t seg;
        if (esp_partition_read(part, off, &seg, sizeof(seg)) != ESP_OK) return 0;
        off += sizeof(seg) + seg.data_len;
        if (off > part->size) return 0;      /* header says more than fits: bogus */
    }
    off += 1;                                /* checksum byte */
    off = (off + 15) & ~((size_t)15);        /* padded to 16 */
    if (hdr.hash_appended) off += 32;        /* SHA-256 */
    return off;
}

size_t petal_ota_image_size(void)
{
    /* Cached: the running image cannot change under us, and the UI polls this to
     * decide whether to offer a transfer at all. */
    static size_t s_cached;
    static bool   s_known;
    if (!s_known) {
        const esp_partition_t *run = esp_ota_get_running_partition();
        s_cached = run ? image_size(run) : 0;
        s_known  = true;
    }
    return s_cached;
}

bool petal_ota_image_read(size_t offset, void *buf, size_t len)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!run) return false;
    return esp_partition_read(run, offset, buf, len) == ESP_OK;
}

petal_ota_t *petal_ota_begin(void)
{
    if (s_writer_busy) return NULL;

    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!slot) {
        ESP_LOGW(TAG, "no spare app slot");
        return NULL;
    }
    if (esp_ota_begin(slot, OTA_SIZE_UNKNOWN, &s_writer.handle) != ESP_OK) {
        ESP_LOGW(TAG, "ota begin failed");
        return NULL;
    }
    s_writer.slot = slot;
    s_writer_busy = true;
    return &s_writer;
}

bool petal_ota_write(petal_ota_t *h, const void *data, size_t len)
{
    if (!h || !s_writer_busy) return false;
    return esp_ota_write(h->handle, data, len) == ESP_OK;
}

bool petal_ota_finish(petal_ota_t *h)
{
    if (!h || !s_writer_busy) return false;
    s_writer_busy = false;

    /* esp_ota_end verifies the image; a transfer that lost bytes must never
     * become the boot partition. */
    if (esp_ota_end(h->handle) != ESP_OK) {
        ESP_LOGW(TAG, "image did not verify");
        return false;
    }
    if (esp_ota_set_boot_partition(h->slot) != ESP_OK) {
        ESP_LOGW(TAG, "could not set boot partition");
        return false;
    }
    return true;
}

void petal_ota_abort(petal_ota_t *h)
{
    if (!h || !s_writer_busy) return;
    esp_ota_abort(h->handle);
    s_writer_busy = false;
}

void petal_ota_mark_valid(void)
{
    /* Without this the bootloader rolls back to the previous slot on the next
     * boot. Called once the UI is actually up, so "it booted" means something. */
    esp_ota_mark_app_valid_cancel_rollback();
}
