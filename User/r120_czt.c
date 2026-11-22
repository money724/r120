#include "r120_czt.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define R120_CZT_PI                 3.14159265358979323846f
#define R120_CZT_MIN_AMPLITUDE     5.0f
#define R120_CZT_POWER_FLOOR       1.0e-15f

/* Local direct CZT: 2400 * 129 complex multiply-accumulate iterations.
 * This is intentionally simple and independently testable. It is not the
 * FFT-accelerated Bluestein form; measure its runtime on actual STM32 later.
 * Static storage avoids using ~29 KiB of task/main stack.
 */
static float s_windowed_i[R120_SIG_MAX_SAMPLES];
static float s_windowed_q[R120_SIG_MAX_SAMPLES];
static float s_grid_power[R120_CZT_GRID_POINTS];
static uint32_t s_selftest_raw[R120_SIG_MAX_SAMPLES];

static uint32_t R120_CZT_Clamp12(float x)
{
    if (x < 0.0f) return 0U;
    if (x > 4095.0f) return 4095U;
    return (uint32_t)(x + 0.5f);
}

static float R120_CZT_PowerAtHz(float freq_hz, uint32_t count,
                                 uint32_t sample_rate_hz)
{
    /* Complex sum of x[n] * exp(-j 2*pi*f*n/fs).
       A fresh sin/cos is computed once per grid frequency (and periodically
       to limit the drift of the recursive oscillator). */
    const float phase_step = -2.0f * R120_CZT_PI * freq_hz /
                             (float)sample_rate_hz;
    const float d_re = cosf(phase_step);
    const float d_im = sinf(phase_step);
    float w_re = 1.0f, w_im = 0.0f;
    float sum_re = 0.0f, sum_im = 0.0f;
    uint32_t n;

    for (n = 0U; n < count; ++n)
    {
        const float i = s_windowed_i[n];
        const float q = s_windowed_q[n];
        const float next_re = w_re * d_re - w_im * d_im;
        const float next_im = w_re * d_im + w_im * d_re;
        sum_re += i * w_re - q * w_im;
        sum_im += i * w_im + q * w_re;
        w_re = next_re;
        w_im = next_im;

        /* Normalize the oscillator every 256 samples. */
        if ((n & 255U) == 255U)
        {
            const float phase = phase_step * (float)(n + 1U);
            w_re = cosf(phase);
            w_im = sinf(phase);
        }
    }
    return sum_re * sum_re + sum_im * sum_im;
}

R120_CZT_Status R120_CZT_RefinePacked(const uint32_t *packed,
                                      uint32_t sample_count,
                                      uint32_t sample_rate_hz,
                                      R120_SIG_Status coarse_status,
                                      const R120_SIG_Result *coarse,
                                      R120_CZT_Result *out)
{
    float mean_i = 0.0f, mean_q = 0.0f;
    float sum_window = 0.0f;
    float start_hz, end_hz, step_hz;
    float max_power = 0.0f, refined_hz;
    float coarse_hz, half_width, min_hz, max_hz;
    uint32_t n, k, peak_index = 0U;

    if (packed == NULL || coarse == NULL || out == NULL ||
        sample_count < 32U || sample_count > R120_SIG_MAX_SAMPLES ||
        sample_rate_hz < 1000U || sample_rate_hz > 1000000U)
    {
        return R120_CZT_BAD_ARGUMENT;
    }

    memset(out, 0, sizeof(*out));

    coarse_hz = coarse->signed_beat_hz;
    if (coarse_status != R120_SIG_OK ||
        coarse->sample_count != sample_count ||
        coarse->sample_rate_hz != sample_rate_hz ||
        !isfinite(coarse_hz) ||
        fabsf(coarse_hz) < (float)R120_SIG_MIN_BEAT_HZ ||
        fabsf(coarse_hz) > (float)R120_SIG_MAX_BEAT_HZ ||
        !isfinite(coarse->amplitude_counts) ||
        coarse->amplitude_counts < R120_CZT_MIN_AMPLITUDE)
    {
        return R120_CZT_BAD_COARSE_ESTIMATE;
    }

    half_width = R120_CZT_HALF_SPAN_BINS * (float)sample_rate_hz /
                 (float)R120_SIG_FFT_SIZE;
    /* Preserve I/Q frequency sign and V0.4's valid beat-frequency limits. */
    if (coarse_hz > 0.0f)
    {
        min_hz = (float)R120_SIG_MIN_BEAT_HZ;
        max_hz = (float)R120_SIG_MAX_BEAT_HZ;
    }
    else
    {
        min_hz = -(float)R120_SIG_MAX_BEAT_HZ;
        max_hz = -(float)R120_SIG_MIN_BEAT_HZ;
    }
    start_hz = fmaxf(coarse_hz - half_width, min_hz);
    end_hz = fminf(coarse_hz + half_width, max_hz);
    if (!isfinite(start_hz) || !isfinite(end_hz) ||
        end_hz <= start_hz)
    {
        return R120_CZT_BAD_COARSE_ESTIMATE;
    }
    step_hz = (end_hz - start_hz) /
              (float)(R120_CZT_GRID_POINTS - 1U);

    /* Exactly the same mean subtraction and Hann window convention as V0.4. */
    for (n = 0U; n < sample_count; ++n)
    {
        uint32_t word = packed[n];
        mean_i += (float)(word & 0x0FFFU);
        mean_q += (float)((word >> 16U) & 0x0FFFU);
    }
    mean_i /= (float)sample_count;
    mean_q /= (float)sample_count;

    for (n = 0U; n < sample_count; ++n)
    {
        const uint32_t word = packed[n];
        const float phase = (2.0f * R120_CZT_PI * (float)n) /
                            (float)(sample_count - 1U);
        const float win = 0.5f * (1.0f - cosf(phase));
        s_windowed_i[n] = ((float)(word & 0x0FFFU) - mean_i) * win;
        s_windowed_q[n] = ((float)((word >> 16U) & 0x0FFFU) - mean_q) * win;
        sum_window += win;
    }
    if (sum_window <= 0.0f) return R120_CZT_BAD_ARGUMENT;

    for (k = 0U; k < R120_CZT_GRID_POINTS; ++k)
    {
        /* Force last endpoint exact to avoid accumulated grid drift. */
        const float freq_hz = (k == R120_CZT_GRID_POINTS - 1U) ?
                               end_hz : start_hz + (float)k * step_hz;
        const float power = R120_CZT_PowerAtHz(freq_hz, sample_count,
                                               sample_rate_hz);
        s_grid_power[k] = power;
        if (power > max_power)
        {
            max_power = power;
            peak_index = k;
        }
    }

    out->sample_count = sample_count;
    out->sample_rate_hz = sample_rate_hz;
    out->grid_points = R120_CZT_GRID_POINTS;
    out->coarse_signed_beat_hz = coarse_hz;
    out->grid_start_hz = start_hz;
    out->grid_end_hz = end_hz;
    out->grid_spacing_hz = step_hz;
    out->peak_index = (uint16_t)peak_index;
    out->peak_amplitude_counts = sqrtf(max_power) / sum_window;

    /* Reject no-tone data and peaks pinned at the local search boundaries:
       a boundary peak may mean the true tone lies outside the coarse window. */
    if (max_power <= R120_CZT_POWER_FLOOR ||
        out->peak_amplitude_counts < R120_CZT_MIN_AMPLITUDE ||
        peak_index == 0U || peak_index == R120_CZT_GRID_POINTS - 1U)
    {
        return R120_CZT_NO_PEAK;
    }

    refined_hz = start_hz + (float)peak_index * step_hz;
    {
        /* Three-point quadratic interpolation on local peak POWER. */
        const float left = s_grid_power[peak_index - 1U];
        const float center = s_grid_power[peak_index];
        const float right = s_grid_power[peak_index + 1U];
        const float denom = left - 2.0f * center + right;
        if (fabsf(denom) > 1.0e-5f * fmaxf(center, 1.0f))
        {
            float offset = 0.5f * (left - right) / denom;
            if (offset > 0.5f) offset = 0.5f;
            if (offset < -0.5f) offset = -0.5f;
            refined_hz += offset * step_hz;
        }
    }

    out->refined_signed_beat_hz = refined_hz;
    out->abs_beat_hz = fabsf(refined_hz);
    return R120_CZT_OK;
}

R120_CZT_Status R120_CZT_RunSelfTest(R120_CZT_Result *out)
{
    R120_SIG_Result coarse;
    R120_SIG_Status coarse_status;
    const float sample_rate = 400000.0f;
    const float signal_hz = 27777.7778f;
    const float amplitude = 680.0f;
    uint32_t n;

    if (out == NULL) return R120_CZT_BAD_ARGUMENT;

    for (n = 0U; n < R120_SIG_MAX_SAMPLES; ++n)
    {
        const float phase = 2.0f * R120_CZT_PI * signal_hz *
                            (float)n / sample_rate;
        const uint32_t i = R120_CZT_Clamp12(2048.0f + amplitude * cosf(phase));
        const uint32_t q = R120_CZT_Clamp12(2030.0f + amplitude * sinf(phase));
        s_selftest_raw[n] = i | (q << 16U);
    }

    coarse_status = R120_SIG_AnalyzePacked(s_selftest_raw,
                                           R120_SIG_MAX_SAMPLES,
                                           (uint32_t)sample_rate,
                                           &coarse);
    return R120_CZT_RefinePacked(s_selftest_raw,
                                 R120_SIG_MAX_SAMPLES,
                                 (uint32_t)sample_rate,
                                 coarse_status, &coarse, out);
}
