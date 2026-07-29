/*
 * hal_battery.c — Li-ion gauge: ADC voltage, then a charge estimate.
 *
 * Two things make this less trivial than "read the pin":
 *
 *   - A lithium cell's voltage says very little about its charge across the flat
 *     3.6-3.9 V plateau, so the mapping is a curve, not a line. The curve below
 *     is a representative LOADED discharge shape rather than a fit to one
 *     specific cell: tuning it to a single unit makes that unit slightly more
 *     accurate and every other one worse.
 *   - The reported percentage has to be steady enough to read. A raw curve
 *     lookup wanders by a point or two between samples, so the value is
 *     EMA-smoothed and then reported through hysteresis that only ever moves in
 *     the physically plausible direction.
 *
 * To re-fit the curve for a different cell, build with -DLOTUS_BATTERY_CALIB=1,
 * run the device from full to empty on battery, then read the CSV the logger in
 * hal_battery_log.c prints on the next boot.
 */
#include "petal_hal.h"
#include "hal_internal.h"
#include "boards/board.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

/* Loaded single-cell Li-ion voltage (mV) -> state of charge (%). Dense across
 * the flat middle, sparse at the steep ends. Must stay sorted high to low. */
typedef struct { int mv; int pct; } soc_point_t;

static const soc_point_t SOC[] = {
    {4180,100}, {4120, 95}, {4060, 90}, {3980, 85}, {3920, 80}, {3870, 75},
    {3830, 70}, {3790, 65}, {3760, 60}, {3730, 55}, {3700, 50}, {3670, 45},
    {3640, 40}, {3610, 35}, {3570, 30}, {3520, 25}, {3470, 20}, {3400, 15},
    {3320, 10}, {3220,  5}, {3100,  2}, {3000,  0},
};
#define SOC_N ((int)(sizeof(SOC) / sizeof(SOC[0])))

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool s_cali_ok;
static bool s_ready;

static float s_ema;        /* smoothed percentage */
static int   s_shown;      /* the integer actually reported */
static bool  s_seeded;

void hal_battery_init(void)
{
#if PETAL_HAS_BATTERY
    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BOARD_BATT_ADC_UNIT };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGW("hal.batt", "adc unit init failed — gauge disabled");
        return;
    }
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BOARD_BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(s_adc, BOARD_BATT_ADC_CHANNEL, &chan_cfg);

    /* Curve fitting uses the per-chip calibration burned into eFuse; without it
     * the fallback below is only roughly right. */
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = BOARD_BATT_ADC_UNIT,
        .chan     = BOARD_BATT_ADC_CHANNEL,
        .atten    = BOARD_BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
    s_ready = true;
#endif
}

bool petal_battery_present(void)
{
    return s_ready;
}

int petal_battery_millivolts(void)
{
    if (!s_ready) return 0;

    /* Average a handful of samples: a single ADC read on this part is noisy
     * enough to move the gauge on its own. */
    const int n = 8;
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BOARD_BATT_ADC_CHANNEL, &raw) == ESP_OK) sum += raw;
    }
    int raw = sum / n;

    int mv;
    if (s_cali_ok) adc_cali_raw_to_voltage(s_cali, raw, &mv);
    else           mv = raw * 3100 / 4095;   /* rough fallback for the 12 dB range */

    return mv * BOARD_BATT_DIVIDER;
}

bool petal_battery_charging(void)
{
    if (!s_ready) return false;
    /* No charge-status line: a reading above any real cell voltage can only be
     * the externally held rail. */
    return petal_battery_millivolts() > BOARD_BATT_CHARGING_MV;
}

static int soc_from_mv(int mv)
{
    if (mv >= SOC[0].mv)         return 100;
    if (mv <= SOC[SOC_N - 1].mv) return 0;
    for (int i = 0; i < SOC_N - 1; i++) {
        int hi = SOC[i].mv, lo = SOC[i + 1].mv;
        if (mv <= hi && mv >= lo) {
            int span = hi - lo;                     /* > 0 */
            int drop = SOC[i].pct - SOC[i + 1].pct; /* >= 0 */
            return SOC[i + 1].pct + (mv - lo) * drop / span;
        }
    }
    return 0;
}

int petal_battery_percent(void)
{
    if (!s_ready) return 0;

    bool charging = petal_battery_charging();

    int inst = soc_from_mv(petal_battery_millivolts());
    if (inst < 0)   inst = 0;
    if (inst > 100) inst = 100;

    /* Seed on the first read so the gauge is right immediately at boot rather
     * than easing up from zero. */
    if (!s_seeded) {
        s_ema    = (float)inst;
        s_shown  = inst;
        s_seeded = true;
        return s_shown;
    }

    s_ema += ((float)inst - s_ema) * 0.10f;

    /* Hysteresis, so the digit stops flickering:
     *   - a large divergence (plugged in, unplugged, cell swapped) snaps,
     *   - otherwise move one point at a time, only once the smoothed value is a
     *     full point past what is shown, and only in the direction the physics
     *     allows. A discharging gauge can therefore never tick back up, which is
     *     what caused the old 77<->78 flip-flop. */
    float diff = s_ema - (float)s_shown;
    if (diff >= 3.0f || diff <= -3.0f) {
        s_shown = (int)(s_ema + 0.5f);
    } else if (charging) {
        if (diff >= 1.0f && s_shown < 100) s_shown++;
    } else {
        if (diff <= -1.0f && s_shown > 0) s_shown--;
    }

    if (s_shown < 0)   s_shown = 0;
    if (s_shown > 100) s_shown = 100;
    return s_shown;
}
