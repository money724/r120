#include "r120_phase.h"
#include "r120_czt.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define R120_PHASE_PI              3.14159265358979323846f
#define R120_PHASE_MIN_AMP_COUNTS  5.0f
#define R120_PHASE_MIN_COHERENCE   0.20f
#define R120_PHASE_TEST_FREQ_HZ    27777.7778f
#define R120_PHASE_TEST_RAD        0.7000000f

/* Only used for the CPU self-test, NOT the real ADC DMA buffer. */
static uint32_t s_phase_test_raw[R120_SIG_MAX_SAMPLES];

static uint32_t R120_PHASE_Clamp12(float value)
{
    if (value < 0.0f) return 0U;
    if (value > 4095.0f) return 4095U;
    return (uint32_t)(value + 0.5f);
}

static float R120_PHASE_Wrap(float rad)
{
    if (!isfinite(rad)) return 0.0f;
    while (rad > R120_PHASE_PI) rad -= 2.0f * R120_PHASE_PI;
    while (rad < -R120_PHASE_PI) rad += 2.0f * R120_PHASE_PI;
    return rad;
}

float R120_PHASE_WrappedDeltaRad(float now_rad, float previous_rad)
{
    if (!isfinite(now_rad) || !isfinite(previous_rad)) return NAN;
    return R120_PHASE_Wrap(now_rad - previous_rad);
}

R120_PHASE_Status R120_PHASE_EstimatePacked(const uint32_t *packed,
                                           uint32_t sample_count,
                                           uint32_t sample_rate_hz,
                                           float signed_beat_hz,
                                           R120_PHASE_Result *out)
{
    uint32_t n;
    float mean_i = 0.0f, mean_q = 0.0f;
    float sum_re = 0.0f, sum_im = 0.0f;
    float sum_win = 0.0f, sum_weighted_power = 0.0f;
    float osc_re = 1.0f, osc_im = 0.0f;
    float d_re, d_im, delta, amp, coherent;

    if (packed == NULL || out == NULL || sample_count < 32U ||
        sample_count > R120_SIG_MAX_SAMPLES || sample_rate_hz < 1000U ||
        sample_rate_hz > 1000000U)
        return R120_PHASE_BAD_ARGUMENT;

    memset(out, 0, sizeof(*out));
    out->sample_count = sample_count;
    out->sample_rate_hz = sample_rate_hz;
    out->signed_beat_hz = signed_beat_hz;

    if (!isfinite(signed_beat_hz) ||
        fabsf(signed_beat_hz) < (float)R120_SIG_MIN_BEAT_HZ ||
        fabsf(signed_beat_hz) > (float)R120_SIG_MAX_BEAT_HZ ||
        fabsf(signed_beat_hz) >= 0.5f * (float)sample_rate_hz)
        return R120_PHASE_BAD_FREQUENCY;

    /* Match V0.4/V0.6 unweighted DC-removal convention. */
    for (n = 0U; n < sample_count; ++n)
    {
        const uint32_t word = packed[n];
        mean_i += (float)(word & 0x0FFFU);
        mean_q += (float)((word >> 16U) & 0x0FFFU);
    }
    mean_i /= (float)sample_count;
    mean_q /= (float)sample_count;
    out->adc_mean_i = mean_i;
    out->adc_mean_q = mean_q;

    /* x[n] * exp(-j * 2*pi*f*n/fs), so the phase is at sample index n=0.
       Recurrence uses sin/cos once for delta, refreshed every 128 samples.
       This is not the absolute phase of the 120GHz carrier. */
    delta = -2.0f * R120_PHASE_PI * signed_beat_hz /
            (float)sample_rate_hz;
    d_re = cosf(delta);
    d_im = sinf(delta);

    for (n = 0U; n < sample_count; ++n)
    {
        const uint32_t word = packed[n];
        const float i = (float)(word & 0x0FFFU) - mean_i;
        const float q = (float)((word >> 16U) & 0x0FFFU) - mean_q;
        const float window_phase = 2.0f * R120_PHASE_PI *
                                   (float)n / (float)(sample_count - 1U);
        const float win = 0.5f * (1.0f - cosf(window_phase));
        const float next_re = osc_re * d_re - osc_im * d_im;
        const float next_im = osc_re * d_im + osc_im * d_re;

        sum_re += win * (i * osc_re - q * osc_im);
        sum_im += win * (i * osc_im + q * osc_re);
        sum_win += win;
        sum_weighted_power += win * (i * i + q * q);

        osc_re = next_re;
        osc_im = next_im;
        if ((n & 127U) == 127U)
        {
            const float absolute_angle = delta * (float)(n + 1U);
            osc_re = cosf(absolute_angle);
            osc_im = sinf(absolute_angle);
        }
    }

    if (sum_win <= 0.0f || sum_weighted_power <= 0.0f)
        return R120_PHASE_WEAK_SIGNAL;

    amp = sqrtf(sum_re * sum_re + sum_im * sum_im) / sum_win;
    coherent = (sum_re * sum_re + sum_im * sum_im) /
               (sum_win * sum_weighted_power);

    if (!isfinite(amp) || !isfinite(coherent) ||
        amp < R120_PHASE_MIN_AMP_COUNTS ||
        coherent < R120_PHASE_MIN_COHERENCE)
        return R120_PHASE_WEAK_SIGNAL;

    if (coherent > 1.0f) coherent = 1.0f; /* floating-point rounding */
    out->phase_rad = atan2f(sum_im, sum_re);
    out->phase_deg = out->phase_rad * (180.0f / R120_PHASE_PI);
    out->amplitude_counts = amp;
    out->coherent_fraction = coherent;
    return R120_PHASE_OK;
}

R120_PHASE_Status R120_PHASE_RunSelfTest(R120_PHASE_Result *out)
{
    R120_SIG_Result fft;
    R120_CZT_Result czt;
    R120_SIG_Status fft_status;
    R120_CZT_Status czt_status;
    R120_PHASE_Status phase_status;
    uint32_t n;
    const float rate_hz = 400000.0f;
    const float amplitude = 680.0f;

    if (out == NULL) return R120_PHASE_BAD_ARGUMENT;

    for (n = 0U; n < R120_SIG_MAX_SAMPLES; ++n)
    {
        const float angle = 2.0f * R120_PHASE_PI * R120_PHASE_TEST_FREQ_HZ *
                            (float)n / rate_hz + R120_PHASE_TEST_RAD;
        const uint32_t i = R120_PHASE_Clamp12(2048.0f + amplitude * cosf(angle));
        const uint32_t q = R120_PHASE_Clamp12(2030.0f + amplitude * sinf(angle));
        s_phase_test_raw[n] = i | (q << 16U);
    }

    fft_status = R120_SIG_AnalyzePacked(s_phase_test_raw,
                                       R120_SIG_MAX_SAMPLES,
                                       (uint32_t)rate_hz, &fft);
    if (fft_status != R120_SIG_OK)
        return R120_PHASE_SELFTEST_FAILED;

    czt_status = R120_CZT_RefinePacked(s_phase_test_raw,
                                       R120_SIG_MAX_SAMPLES,
                                       (uint32_t)rate_hz,
                                       fft_status, &fft, &czt);
    if (czt_status != R120_CZT_OK)
        return R120_PHASE_SELFTEST_FAILED;

    phase_status = R120_PHASE_EstimatePacked(s_phase_test_raw,
                                             R120_SIG_MAX_SAMPLES,
                                             (uint32_t)rate_hz,
                                             czt.refined_signed_beat_hz,
                                             out);
    if (phase_status != R120_PHASE_OK)
        return R120_PHASE_SELFTEST_FAILED;

    if (fabsf(czt.refined_signed_beat_hz - R120_PHASE_TEST_FREQ_HZ) > 2.0f ||
        fabsf(R120_PHASE_WrappedDeltaRad(out->phase_rad,
                                         R120_PHASE_TEST_RAD)) > 0.03f ||
        out->coherent_fraction < 0.95f)
        return R120_PHASE_SELFTEST_FAILED;

    return R120_PHASE_OK;
}
