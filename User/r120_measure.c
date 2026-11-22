#include "r120_measure.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define R120_MEAS_PI 3.14159265358979323846f

/* Test buffer is NOT a DMA buffer. Built-in self-test is not reentrant. */
static uint32_t s_selftest_raw[R120_SIG_MAX_SAMPLES];

static uint32_t Clamp12(float x)
{
    if (x <= 0.0f) return 0U;
    if (x >= 4095.0f) return 4095U;
    return (uint32_t)(x + 0.5f);
}

static int ConfigValid(const R120_MEAS_Config *cfg)
{
    R120_RANGE_Config range_check;
    if (cfg == NULL ||
        cfg->sample_count < 32U || cfg->sample_count > R120_SIG_MAX_SAMPLES ||
        cfg->sample_rate_hz < 1000U || cfg->sample_rate_hz > 1000000U ||
        !isfinite(cfg->min_peak_contrast_db) ||
        cfg->min_peak_contrast_db < 0.0f || cfg->min_peak_contrast_db > 120.0f ||
        !isfinite(cfg->min_amplitude_counts) ||
        cfg->min_amplitude_counts < 0.0f || cfg->min_amplitude_counts > 4095.0f ||
        !isfinite(cfg->min_coherent_fraction) ||
        cfg->min_coherent_fraction < 0.0f || cfg->min_coherent_fraction > 1.0f ||
        !isfinite(cfg->max_clipped_fraction) ||
        cfg->max_clipped_fraction < 0.0f || cfg->max_clipped_fraction > 1.0f)
        return 0;

    /* Reuse V0.5 validation rather than maintaining another sweep formula. */
    return (R120_RANGE_MakeConfig(cfg->range.bandwidth_hz,
                                  cfg->range.duration_us,
                                  cfg->range.min_distance_m,
                                  cfg->range.max_distance_m,
                                  cfg->range.distance_offset_m,
                                  &range_check) == R120_RANGE_OK);
}

R120_MEAS_Status R120_MEAS_MakeConfig(const R120_RANGE_Config *range,
                                     uint32_t sample_rate_hz,
                                     uint32_t sample_count,
                                     R120_MEAS_Config *out)
{
    if (out == NULL || range == NULL) return R120_MEAS_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->range = *range;
    out->sample_rate_hz = sample_rate_hz;
    out->sample_count = sample_count;
    out->min_peak_contrast_db = 18.0f;
    out->min_amplitude_counts = 20.0f;
    out->min_coherent_fraction = 0.70f;
    out->max_clipped_fraction = 0.005f;  /* 0.5% */
    return ConfigValid(out) ? R120_MEAS_OK : R120_MEAS_BAD_CONFIG;
}

R120_MEAS_Status R120_MEAS_AnalyzePacked(const uint32_t *packed,
                                         uint32_t sample_count,
                                         const R120_MEAS_Config *cfg,
                                         R120_MEAS_Result *out)
{
    uint32_t n;
    if (out == NULL) return R120_MEAS_BAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->status = R120_MEAS_BAD_ARGUMENT;

    if (packed == NULL || cfg == NULL) return out->status;
    if (!ConfigValid(cfg)) {
        out->status = R120_MEAS_BAD_CONFIG;
        return out->status;
    }
    if (sample_count != cfg->sample_count) {
        out->status = R120_MEAS_BAD_SAMPLES;
        return out->status;
    }
    /* A complete normal-DMA frame MUST have been captured and stopped first.
       We cannot detect partial DMA or a misaligned chirp from this data. */
    for (n = 0U; n < sample_count; ++n) {
        const uint32_t raw = packed[n];
        const uint32_t i = raw & 0x0FFFU;
        const uint32_t q = (raw >> 16U) & 0x0FFFU;
        if (((raw & 0xF000F000U) != 0U)) {
            out->status = R120_MEAS_BAD_SAMPLES;
            return out->status;
        }
        if (i <= 1U || i >= 4094U || q <= 1U || q >= 4094U)
            ++out->clipped_samples;
    }
    out->clipped_fraction = (float)out->clipped_samples / (float)sample_count;
    if (out->clipped_fraction > cfg->max_clipped_fraction) {
        out->status = R120_MEAS_CLIPPED;
        return out->status;
    }

    out->fft_status = R120_SIG_AnalyzePacked(packed, sample_count,
                                             cfg->sample_rate_hz, &out->fft);
    if (out->fft_status != R120_SIG_OK) {
        out->status = R120_MEAS_NO_TARGET;
        return out->status;
    }
    if (!isfinite(out->fft.peak_contrast_db) ||
        out->fft.peak_contrast_db < cfg->min_peak_contrast_db ||
        !isfinite(out->fft.amplitude_counts) ||
        out->fft.amplitude_counts < cfg->min_amplitude_counts) {
        out->status = R120_MEAS_NO_TARGET;
        return out->status;
    }

    out->czt_status = R120_CZT_RefinePacked(packed, sample_count,
                                             cfg->sample_rate_hz,
                                             out->fft_status, &out->fft,
                                             &out->czt);
    if (out->czt_status != R120_CZT_OK) {
        out->status = R120_MEAS_CZT_FAILED;
        return out->status;
    }

    out->range_status = R120_RANGE_FromBeatHz(
                            out->czt.refined_signed_beat_hz,
                            &cfg->range, &out->range);
    if (out->range_status != R120_RANGE_OK) {
        out->status = R120_MEAS_RANGE_FAILED;
        return out->status;
    }
    out->distance_valid = 1U;

    out->phase_status = R120_PHASE_EstimatePacked(
                            packed, sample_count, cfg->sample_rate_hz,
                            out->czt.refined_signed_beat_hz, &out->phase);
    if (out->phase_status != R120_PHASE_OK ||
        !isfinite(out->phase.coherent_fraction) ||
        out->phase.coherent_fraction < cfg->min_coherent_fraction) {
        /* Keep frequency-only range as valid, but phase must not be fused. */
        out->phase_valid = 0U;
        out->status = R120_MEAS_LOW_QUALITY;
        return out->status;
    }
    out->phase_valid = 1U;
    out->status = R120_MEAS_OK;
    return out->status;
}

R120_MEAS_Status R120_MEAS_RunSelfTest(R120_MEAS_Result *out)
{
    R120_MEAS_Config cfg;
    R120_RANGE_Config range;
    double f_hz = 0.0;
    uint32_t n;
    R120_MEAS_Status status;
    if (out == NULL) return R120_MEAS_BAD_ARGUMENT;

    if (R120_RANGE_MakeConfig(UINT64_C(5000000000), 6000U,
                              0.0, 5.2, 0.0, &range) != R120_RANGE_OK ||
        R120_MEAS_MakeConfig(&range, 400000U, 2400U, &cfg) != R120_MEAS_OK ||
        R120_RANGE_ExpectedBeatHz(5.0, &range, &f_hz) != R120_RANGE_OK)
        return R120_MEAS_SELFTEST_FAILED;

    for (n = 0U; n < 2400U; ++n) {
        const float phase = 2.0f * R120_MEAS_PI * (float)f_hz *
                             (float)n / 400000.0f + 0.7000f;
        const uint32_t i = Clamp12(2048.0f + 680.0f * cosf(phase));
        const uint32_t q = Clamp12(2030.0f + 680.0f * sinf(phase));
        s_selftest_raw[n] = i | (q << 16U);
    }
    status = R120_MEAS_AnalyzePacked(s_selftest_raw, 2400U, &cfg, out);
    if (status != R120_MEAS_OK ||
        fabs(out->range.distance_m - 5.0) > 0.015 ||
        fabsf(R120_PHASE_WrappedDeltaRad(out->phase.phase_rad, 0.7000f)) > 0.05f ||
        out->phase.coherent_fraction < 0.9f) {
        out->status = R120_MEAS_SELFTEST_FAILED;
        return out->status;
    }
    return R120_MEAS_OK;
}
