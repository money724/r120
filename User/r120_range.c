#include "r120_range.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* These are SOFTWARE numerical safeguards, not hardware lock guarantees.
 * TRX120's real sweep span must be checked on the board separately.
 */
#define R120_RANGE_MAX_BW_HZ   UINT64_C(10000000000)
/* V0.2 planner sets this tag on a successfully generated plan. */
#define R120_RANGE_ADF_VALID_TAG UINT32_C(0x415DF159)
#define R120_RANGE_MIN_US      100U
#define R120_RANGE_MAX_US      1000000U

static int R120_RANGE_ConfigValid(const R120_RANGE_Config *cfg)
{
    if (cfg == NULL ||
        cfg->bandwidth_hz == 0U ||
        cfg->bandwidth_hz > R120_RANGE_MAX_BW_HZ ||
        cfg->duration_us < R120_RANGE_MIN_US ||
        cfg->duration_us > R120_RANGE_MAX_US ||
        !isfinite(cfg->min_distance_m) ||
        !isfinite(cfg->max_distance_m) ||
        !isfinite(cfg->distance_offset_m) ||
        cfg->min_distance_m < 0.0 ||
        cfg->max_distance_m <= cfg->min_distance_m ||
        cfg->max_distance_m > 10000.0 ||
        fabs(cfg->distance_offset_m) > 10000.0)
    {
        return 0;
    }
    return 1;
}

R120_RANGE_Status R120_RANGE_MakeConfig(uint64_t bandwidth_hz,
                                        uint32_t duration_us,
                                        double min_distance_m,
                                        double max_distance_m,
                                        double distance_offset_m,
                                        R120_RANGE_Config *out)
{
    R120_RANGE_Config candidate;
    if (out == NULL) return R120_RANGE_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));

    candidate.bandwidth_hz = bandwidth_hz;
    candidate.duration_us = duration_us;
    candidate.min_distance_m = min_distance_m;
    candidate.max_distance_m = max_distance_m;
    candidate.distance_offset_m = distance_offset_m;
    if (!R120_RANGE_ConfigValid(&candidate)) return R120_RANGE_BAD_SWEEP;
    *out = candidate;
    return R120_RANGE_OK;
}

R120_RANGE_Status R120_RANGE_ConfigFromPlan(const ADF4159_CFG_Plan *plan,
                                            double min_distance_m,
                                            double max_distance_m,
                                            double distance_offset_m,
                                            R120_RANGE_Config *out)
{
    if (out == NULL) return R120_RANGE_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (plan == NULL || plan->valid_tag != R120_RANGE_ADF_VALID_TAG ||
        plan->mode != ADF4159_CFG_SINGLE_SAWTOOTH ||
        plan->steps < 2U ||
        plan->actual_bandwidth_hz == 0U ||
        plan->actual_duration_us == 0U)
    {
        return R120_RANGE_BAD_SWEEP;
    }
    return R120_RANGE_MakeConfig(plan->actual_bandwidth_hz,
                                  plan->actual_duration_us,
                                  min_distance_m, max_distance_m,
                                  distance_offset_m, out);
}

R120_RANGE_Status R120_RANGE_FromBeatHz(float signed_beat_hz,
                                        const R120_RANGE_Config *cfg,
                                        R120_RANGE_Result *out)
{
    double fb;
    double slope;
    double raw;
    if (out == NULL || cfg == NULL) return R120_RANGE_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!R120_RANGE_ConfigValid(cfg)) return R120_RANGE_BAD_SWEEP;
    if (!isfinite(signed_beat_hz) || signed_beat_hz == 0.0f)
        return R120_RANGE_NO_VALID_PEAK;

    fb = fabs((double)signed_beat_hz);
    slope = (double)cfg->bandwidth_hz * 1000000.0 /
            (double)cfg->duration_us;
    raw = (R120_RANGE_LIGHT_SPEED_MPS * fb) / (2.0 * slope);

    out->signed_beat_hz = signed_beat_hz;
    out->abs_beat_hz = fabsf(signed_beat_hz);
    out->slope_hz_per_s = slope;
    out->raw_distance_m = raw;
    out->distance_m = raw + cfg->distance_offset_m;

    if (!isfinite(out->distance_m) ||
        out->distance_m < cfg->min_distance_m ||
        out->distance_m > cfg->max_distance_m)
        return R120_RANGE_OUT_OF_RANGE;
    return R120_RANGE_OK;
}

R120_RANGE_Status R120_RANGE_FromSignal(R120_SIG_Status signal_status,
                                        const R120_SIG_Result *signal,
                                        const R120_RANGE_Config *cfg,
                                        R120_RANGE_Result *out)
{
    if (out == NULL || cfg == NULL) return R120_RANGE_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (signal_status != R120_SIG_OK || signal == NULL)
        return R120_RANGE_NO_VALID_PEAK;
    if (signal->sample_count < 32U ||
        signal->sample_rate_hz == 0U ||
        !isfinite(signal->signed_beat_hz) ||
        !isfinite(signal->abs_beat_hz) ||
        fabsf(fabsf(signal->signed_beat_hz) - signal->abs_beat_hz) > 1.0f)
        return R120_RANGE_NO_VALID_PEAK;
    return R120_RANGE_FromBeatHz(signal->signed_beat_hz, cfg, out);
}

R120_RANGE_Status R120_RANGE_ExpectedBeatHz(double raw_distance_m,
                                            const R120_RANGE_Config *cfg,
                                            double *out_beat_hz)
{
    double slope;
    if (out_beat_hz == NULL || cfg == NULL)
        return R120_RANGE_BAD_ARGUMENT;
    *out_beat_hz = 0.0;
    if (!R120_RANGE_ConfigValid(cfg)) return R120_RANGE_BAD_SWEEP;
    if (!isfinite(raw_distance_m) || raw_distance_m < 0.0)
        return R120_RANGE_BAD_ARGUMENT;
    slope = (double)cfg->bandwidth_hz * 1000000.0 /
            (double)cfg->duration_us;
    *out_beat_hz = (2.0 * slope * raw_distance_m) /
                   R120_RANGE_LIGHT_SPEED_MPS;
    return R120_RANGE_OK;
}

R120_RANGE_Status R120_RANGE_RunSelfTest(R120_RANGE_Result *out)
{
    R120_SIG_Result signal;
    R120_SIG_Status sig_status;
    R120_RANGE_Config cfg;
    R120_RANGE_Status range_status;
    if (out == NULL) return R120_RANGE_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));

    range_status = R120_RANGE_MakeConfig(UINT64_C(5000000000), 6000U,
                                         0.0, 5.0, 0.0, &cfg);
    if (range_status != R120_RANGE_OK)
        return R120_RANGE_SELFTEST_FAILED;

    sig_status = R120_SIG_RunSelfTest(&signal);
    range_status = R120_RANGE_FromSignal(sig_status, &signal, &cfg, out);
    if (range_status != R120_RANGE_OK ||
        fabs(out->distance_m - 4.99654097) > 0.02)
        return R120_RANGE_SELFTEST_FAILED;
    return R120_RANGE_OK;
}
