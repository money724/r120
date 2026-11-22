#ifndef R120_PHASE_H
#define R120_PHASE_H

#include <stdint.h>
#include "r120_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* V0.7: Estimate the phase of a dominant complex IF beat I+jQ.
 * The phase reference is sample n=0 of THIS acquisition frame.
 * This is NOT the 120 GHz RF propagation phase, nor an unambiguous range.
 * Foreground only; never call from an interrupt or while DMA is writing.
 */

typedef enum
{
    R120_PHASE_OK = 0,
    R120_PHASE_BAD_ARGUMENT = 1,
    R120_PHASE_BAD_FREQUENCY = 2,
    R120_PHASE_WEAK_SIGNAL = 3,
    R120_PHASE_SELFTEST_FAILED = 4
} R120_PHASE_Status;

typedef struct
{
    uint32_t sample_count;
    uint32_t sample_rate_hz;
    float signed_beat_hz;
    float phase_rad;           /* Wrapped to [-pi, pi], at sample index zero. */
    float phase_deg;
    float amplitude_counts;    /* Approximate coherent complex-tone amplitude. */
    float coherent_fraction;   /* 0..1 matched-tone energy fraction; NOT SNR. */
    float adc_mean_i;
    float adc_mean_q;
} R120_PHASE_Result;

/* Given a PREVIOUSLY estimated signed beat frequency (usually from CZT),
 * estimate complex-tone phase via Hann-weighted coherent demodulation.
 * The same immutable packed I/Q frame must have been used for the frequency.
 * Packed: ADC1 I in low 16 bits, ADC2 Q in high 16 bits, 12-bit aligned.
 */
R120_PHASE_Status R120_PHASE_EstimatePacked(const uint32_t *packed,
                                           uint32_t sample_count,
                                           uint32_t sample_rate_hz,
                                           float signed_beat_hz,
                                           R120_PHASE_Result *out);

/* Relative phase between two estimates, wrapped to [-pi, pi].
 * Valid only if their sampling time origin, frequencies and IQ calibration
 * have been made comparable; NOT a standalone displacement measurement.
 */
float R120_PHASE_WrappedDeltaRad(float now_rad, float previous_rad);

/* CPU-only self-test. Generates simulated I/Q, runs V0.4 FFT and V0.6 CZT
 * on THE SAME frame, then estimates its phase. No GPIO/PLL/ADC/DMA calls.
 * Returns R120_PHASE_OK only if frequency and phase are within tolerance.
 */
R120_PHASE_Status R120_PHASE_RunSelfTest(R120_PHASE_Result *out);

#ifdef __cplusplus
}
#endif

#endif /* R120_PHASE_H */
