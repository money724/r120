#ifndef R120_CZT_H
#define R120_CZT_H

#include <stdint.h>
#include "r120_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local frequency-grid refinement of a dominant complex I+jQ beat.
 * This is a direct, local chirp-Z-transform (CZT) evaluation:
 *   X[k] = sum_{n=0}^{N-1} x[n] * exp(-j*2*pi*(f0+k*df)*n/fs)
 * Direct O(N*M), without a separate FFT convolution library.
 * A denser frequency grid is INTERPOLATION, not increased physical resolution.
 * Not IRQ-safe / not reentrant; uses static workspace.
 */
#define R120_CZT_GRID_POINTS        129U
#define R120_CZT_HALF_SPAN_BINS     1.5f  /* +/-(1.5 * fs/4096) */

typedef enum
{
    R120_CZT_OK = 0,
    R120_CZT_BAD_ARGUMENT = 1,
    R120_CZT_BAD_COARSE_ESTIMATE = 2,
    R120_CZT_NO_PEAK = 3
} R120_CZT_Status;

typedef struct
{
    uint32_t sample_count;
    uint32_t sample_rate_hz;
    uint32_t grid_points;
    float coarse_signed_beat_hz;
    float refined_signed_beat_hz;
    float abs_beat_hz;
    float grid_start_hz;
    float grid_end_hz;
    float grid_spacing_hz;
    float peak_amplitude_counts; /* Complex sinusoid amplitude approx. */
    uint16_t peak_index;
} R120_CZT_Result;

/* Call after R120_SIG_AnalyzePacked() has returned R120_SIG_OK.
 * 'packed' must refer to the SAME immutable data frame as the coarse FFT.
 * Low 16 bits = ADC1 I, high 16 bits = ADC2 Q, both 12-bit right aligned.
 * Intended for foreground processing after DMA and timer have stopped.
 * No GPIO, PLL, ADC, TIM, or DMA access.
 */
R120_CZT_Status R120_CZT_RefinePacked(const uint32_t *packed,
                                      uint32_t sample_count,
                                      uint32_t sample_rate_hz,
                                      R120_SIG_Status coarse_status,
                                      const R120_SIG_Result *coarse,
                                      R120_CZT_Result *out);

/* Pure CPU test. Generates a synthetic +27.7778 kHz I/Q tone and uses
 * V0.4 FFT before applying V0.6 CZT refinement.
 * Does NOT run/trigger real hardware.
 */
R120_CZT_Status R120_CZT_RunSelfTest(R120_CZT_Result *out);

#ifdef __cplusplus
}
#endif

#endif /* R120_CZT_H */
