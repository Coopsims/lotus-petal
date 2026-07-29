/*
 * hal_battery_log.c — discharge logger, for fitting the battery curve.
 *
 * Compiled in only when the build defines LOTUS_BATTERY_CALIB
 * (idf.py -DLOTUS_BATTERY_CALIB=1 build), so a production image never touches
 * flash for it. When enabled it samples the raw cell voltage to storage every
 * 60 s — surviving a brownout — and prints the whole log as CSV over serial on
 * boot. Run the device full to empty on battery, plug it back in, and read the
 * curve out of the boot log.
 */
#include "petal_hal.h"
#include "hal_internal.h"

#ifdef LOTUS_BATTERY_CALIB

#include <stdio.h>

#define LOG_NS        "battlog"
#define LOG_KEY       "mv"
#define LOG_MAX       1200          /* 1200 samples at 60 s = 20 h of headroom */
#define LOG_PERIOD_MS 60000

/* Fixed-size blob with an explicit count: a boot marker is a stored zero, so the
 * used length cannot be recovered by scanning for trailing zeros. */
typedef struct {
    uint16_t n;
    uint16_t mv[LOG_MAX];
} battlog_t;

static battlog_t s_log;

static void log_save(void)
{
    petal_kv_set(LOG_NS, LOG_KEY, &s_log, sizeof(s_log));
}

static void log_load(void)
{
    if (!petal_kv_get(LOG_NS, LOG_KEY, &s_log, sizeof(s_log)) || s_log.n > LOG_MAX) {
        s_log.n = 0;
    }
}

/* CSV to serial. A 0 is a boot marker separating sessions; the run you want is
 * the monotonically falling segment just before a marker. */
static void log_dump(void)
{
    printf("\nBATTLOG_BEGIN n=%u interval_s=60 (0=boot marker)\n", s_log.n);
    for (uint16_t i = 0; i < s_log.n; i++) printf("BATTLOG %u %u\n", i, s_log.mv[i]);
    printf("BATTLOG_END\n");
}

static void sample_cb(lv_timer_t *t)
{
    (void)t;
    if (s_log.n >= LOG_MAX) return;        /* full: the log is complete */
    s_log.mv[s_log.n++] = (uint16_t)petal_battery_millivolts();
    log_save();
}

void hal_battery_log_init(void)
{
    log_load();
    log_dump();                            /* surface whatever survived, every boot */

    if (s_log.n < LOG_MAX) {               /* mark the session boundary */
        s_log.mv[s_log.n++] = 0;
        log_save();
    }
    lv_timer_create(sample_cb, LOG_PERIOD_MS, NULL);
}

#else  /* !LOTUS_BATTERY_CALIB */

void hal_battery_log_init(void) { }

#endif
