#include "r120_signal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define R120_SIG_PI 3.14159265358979323846f
#define R120_SIG_MIN_AMPLITUDE_COUNTS 5.0f
#define R120_SIG_MIN_PEAK_CONTRAST_DB 18.0f

/* Static storage keeps ~32 KiB FFT workspace off the task/main stack.
 * Only call R120_SIG_AnalyzePacked from foreground code, never an ISR.
 * All fields are plain floats; Cortex-M7 FPU can execute them efficiently.
 */
typedef struct
{
    float re;
    float im;
} R120_SIG_Complex;

static R120_SIG_Complex s_fft[R120_SIG_FFT_SIZE];
static uint32_t s_test_samples[R120_SIG_MAX_SAMPLES];

static float R120_SIG_PowerAt(uint32_t index)
{
    float re = s_fft[index].re;
    float im = s_fft[index].im;
    return re * re + im * im;
}

static uint32_t R120_SIG_Clamp12(float value)
{
    if (value < 0.0f) return 0U;
    if (value > 4095.0f) return 4095U;
    return (uint32_t)(value + 0.5f);
}

/* In-place 4096-point forward radix-2 DIT complex FFT. */
static void R120_SIG_FFT4096(void)
{
    uint32_t i, j, bit, len;

    /* Bit reversal. */
    for (i = 1U, j = 0U; i < R120_SIG_FFT_SIZE; ++i)
    {
        bit = R120_SIG_FFT_SIZE >> 1U;
        while ((j & bit) != 0U)
        {
            j ^= bit;
            bit >>= 1U;
        }
        j ^= bit;
        if (i < j)
        {
            R120_SIG_Complex tmp = s_fft[i];
            s_fft[i] = s_fft[j];
            s_fft[j] = tmp;
        }
    }

    for (len = 2U; len <= R120_SIG_FFT_SIZE; len <<= 1U)
    {
        const uint32_t half = len >> 1U;
        const float angle = -2.0f * R120_SIG_PI / (float)len;
        const float step_re = cosf(angle);
        const float step_im = sinf(angle);
        uint32_t base;

        for (base = 0U; base < R120_SIG_FFT_SIZE; base += len)
        {
            float wr = 1.0f;
            float wi = 0.0f;
            uint32_t k;
            for (k = 0U; k < half; ++k)
            {
                const uint32_t ia = base + k;
                const uint32_t ib = ia + half;
                const float br = s_fft[ib].re;
                const float bi = s_fft[ib].im;
                const float tr = wr * br - wi * bi;
                const float ti = wr * bi + wi * br;
                const float ar = s_fft[ia].re;
                const float ai = s_fft[ia].im;
                const float next_wr = wr * step_re - wi * step_im;
                const float next_wi = wr * step_im + wi * step_re;

                s_fft[ia].re = ar + tr;
                s_fft[ia].im = ai + ti;
                s_fft[ib].re = ar - tr;
                s_fft[ib].im = ai - ti;
                wr = next_wr;
                wi = next_wi;
            }
        }
    }
}

R120_SIG_Status R120_SIG_AnalyzePacked(const uint32_t *packed,
                                      uint32_t sample_count,
                                      uint32_t sample_rate_hz,
                                      R120_SIG_Result *out)
{
    uint32_t n, k, peak_index = 0U;
    uint32_t valid_count = 0U;
    uint32_t min_bin, max_bin;
    uint32_t other_count = 0U;
    float mean_i = 0.0f, mean_q = 0.0f;
    float window_sum = 0.0f;
    float peak_power = 0.0f;
    float other_power_sum = 0.0f;
    float interp = 0.0f;
    float contrast_db, amp, signed_bin;

    if (out == NULL || packed == NULL || sample_count < 32U ||
        sample_count > R120_SIG_MAX_SAMPLES ||
        sample_rate_hz < 1000U || sample_rate_hz > 1000000U)
    {
        return R120_SIG_BAD_ARGUMENT;
    }

    memset(out, 0, sizeof(*out));
    out->sample_count = sample_count;
    out->sample_rate_hz = sample_rate_hz;

    /* Mean removal is essential: an ADC bias around 2048 otherwise makes
       the DC bin dominate the spectrum. */
    for (n = 0U; n < sample_count; ++n)
    {
        const uint32_t v = packed[n];
        mean_i += (float)(v & 0x0FFFU);
        mean_q += (float)((v >> 16U) & 0x0FFFU);
    }
    mean_i /= (float)sample_count;
    mean_q /= (float)sample_count;
    out->adc_mean_i = mean_i;
    out->adc_mean_q = mean_q;

    memset(s_fft, 0, sizeof(s_fft));

    for (n = 0U; n < sample_count; ++n)
    {
        const float phase = (2.0f * R120_SIG_PI * (float)n) /
                            (float)(sample_count - 1U);
        const float w = 0.5f * (1.0f - cosf(phase));
        const uint32_t v = packed[n];
        const float i = (float)(v & 0x0FFFU) - mean_i;
        const float q = (float)((v >> 16U) & 0x0FFFU) - mean_q;
        s_fft[n].re = i * w;
        s_fft[n].im = q * w;
        window_sum += w;
    }

    R120_SIG_FFT4096();

    /* Work on the entire complex spectrum; I/Q mixer polarity may put the
       beat at positive or negative frequency. Exclude near-DC and Nyquist. */
    min_bin = (uint32_t)(((uint64_t)R120_SIG_MIN_BEAT_HZ *
                          R120_SIG_FFT_SIZE + sample_rate_hz - 1U) /
                         sample_rate_hz);
    max_bin = (uint32_t)(((uint64_t)R120_SIG_MAX_BEAT_HZ *
                          R120_SIG_FFT_SIZE) / sample_rate_hz);
    if (min_bin < 3U) min_bin = 3U;
    if (max_bin > (R120_SIG_FFT_SIZE / 2U - 3U))
    {
        max_bin = R120_SIG_FFT_SIZE / 2U - 3U;
    }
    if (max_bin <= min_bin) return R120_SIG_BAD_ARGUMENT;

    for (k = 0U; k < R120_SIG_FFT_SIZE; ++k)
    {
        const uint32_t absk = (k <= (R120_SIG_FFT_SIZE / 2U)) ?
                               k : (R120_SIG_FFT_SIZE - k);
        float p;
        if (absk < min_bin || absk > max_bin) continue;
        p = R120_SIG_PowerAt(k);
        ++valid_count;
        if (p > peak_power)
        {
            peak_power = p;
            peak_index = k;
        }
    }

    if (valid_count < 8U || peak_power <= 0.0f || window_sum <= 0.0f)
    {
        return R120_SIG_NO_CLEAR_PEAK;
    }

    /* Exclude 7 bins around the dominant tone when estimating the average
       spectral background. This is a contrast metric, not noise calibration. */
    for (k = 0U; k < R120_SIG_FFT_SIZE; ++k)
    {
        const uint32_t absk = (k <= (R120_SIG_FFT_SIZE / 2U)) ?
                               k : (R120_SIG_FFT_SIZE - k);
        uint32_t dist = (k >= peak_index) ? (k - peak_index) :
                                              (peak_index - k);
        if (dist > R120_SIG_FFT_SIZE / 2U)
            dist = R120_SIG_FFT_SIZE - dist;
        if (absk < min_bin || absk > max_bin || dist <= 3U)
            continue;
        other_power_sum += R120_SIG_PowerAt(k);
        ++other_count;
    }
    if (other_count == 0U) return R120_SIG_NO_CLEAR_PEAK;

    /* No log of zero/negative: use a tiny positive lower bound. */
    if (other_power_sum < 1.0e-12f) other_power_sum = 1.0e-12f;
    contrast_db = 10.0f * log10f(peak_power /
                              (other_power_sum / (float)other_count));
    amp = sqrtf(peak_power) / window_sum;

    out->peak_contrast_db = contrast_db;
    out->amplitude_counts = amp;
    out->peak_bin = (uint16_t)peak_index;

    if (amp < R120_SIG_MIN_AMPLITUDE_COUNTS ||
        contrast_db < R120_SIG_MIN_PEAK_CONTRAST_DB)
    {
        return R120_SIG_NO_CLEAR_PEAK;
    }

    /* Three-bin logarithmic-power parabolic interpolation. Approximate
       correction for bin quantization, not an accuracy guarantee. */
    {
        const uint32_t left = (peak_index + R120_SIG_FFT_SIZE - 1U) %
                              R120_SIG_FFT_SIZE;
        const uint32_t right = (peak_index + 1U) % R120_SIG_FFT_SIZE;
        const float floor_p = 1.0e-20f;
        const float l = logf(fmaxf(R120_SIG_PowerAt(left), floor_p));
        const float c = logf(fmaxf(peak_power, floor_p));
        const float r = logf(fmaxf(R120_SIG_PowerAt(right), floor_p));
        const float denom = l - 2.0f * c + r;
        if (fabsf(denom) > 1.0e-6f)
        {
            interp = 0.5f * (l - r) / denom;
            if (interp > 0.5f) interp = 0.5f;
            if (interp < -0.5f) interp = -0.5f;
        }
    }

    signed_bin = (peak_index <= R120_SIG_FFT_SIZE / 2U) ?
                  (float)peak_index :
                  ((float)peak_index - (float)R120_SIG_FFT_SIZE);
    out->signed_beat_hz = (signed_bin + interp) *
                          (float)sample_rate_hz / (float)R120_SIG_FFT_SIZE;
    out->abs_beat_hz = fabsf(out->signed_beat_hz);
    return R120_SIG_OK;
}

R120_SIG_Status R120_SIG_RunSelfTest(R120_SIG_Result *out)
{
    uint32_t n;
    const float sample_rate = 400000.0f;
    const float signal_hz = 27777.7778f; /* 5-m ideal beat, 5GHz / 6ms. */
    const float amplitude = 680.0f;
    for (n = 0U; n < R120_SIG_MAX_SAMPLES; ++n)
    {
        const float phase = 2.0f * R120_SIG_PI * signal_hz *
                            (float)n / sample_rate;
        const uint32_t i = R120_SIG_Clamp12(2048.0f + amplitude * cosf(phase));
        const uint32_t q = R120_SIG_Clamp12(2030.0f + amplitude * sinf(phase));
        s_test_samples[n] = i | (q << 16U);
    }
    return R120_SIG_AnalyzePacked(s_test_samples, R120_SIG_MAX_SAMPLES,
                                  (uint32_t)sample_rate, out);
}
