#ifndef R120_SIGNAL_H
#define R120_SIGNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Current R120 acquisition: 2400 dual-ADC pairs, 400 kS/s. */
#define R120_SIG_MAX_SAMPLES       2400U
#define R120_SIG_FFT_SIZE          4096U
#define R120_SIG_MIN_BEAT_HZ       500U
#define R120_SIG_MAX_BEAT_HZ       180000U

typedef enum
{
    R120_SIG_OK = 0,
    R120_SIG_BAD_ARGUMENT = 1,
    R120_SIG_NO_CLEAR_PEAK = 2
} R120_SIG_Status;

typedef struct
{
    uint32_t sample_count;
    uint32_t sample_rate_hz;
    float adc_mean_i;
    float adc_mean_q;
    float signed_beat_hz;    /* Complex I+jQ spectrum: may be negative. */
    float abs_beat_hz;       /* Use this for range estimation. */
    float amplitude_counts;  /* Approx. coherent complex tone amplitude. */
    float peak_contrast_db;  /* Peak / mean spectrum power; NOT calibrated SNR. */
    uint16_t peak_bin;
} R120_SIG_Result;

/*
 * Foreground only, not IRQ-safe/reentrant: uses a private 4096-point FFT scratch
 * buffer. Packed input layout: ADC1 I in low 16 bits, ADC2 Q in high 16 bits.
 * ADC readings are 12-bit right aligned. Input is never modified.
 *
 * Functions do not touch PLL, ADC, TIM, DMA, or any GPIO.
 * The spectrum estimates a single dominant complex beat; multi-target processing
 * and calibration are not implemented in this version.
 */
R120_SIG_Status R120_SIG_AnalyzePacked(const uint32_t *packed,
                                      uint32_t sample_count,
                                      uint32_t sample_rate_hz,
                                      R120_SIG_Result *out);

/* Simulates a +27.7778 kHz I/Q tone, 400 kS/s, 2400 pairs.
 * SelfTest is purely computational, so it is safe without the radar board.
 */
R120_SIG_Status R120_SIG_RunSelfTest(R120_SIG_Result *out);

#ifdef __cplusplus
}
#endif

#endif /* R120_SIGNAL_H */
