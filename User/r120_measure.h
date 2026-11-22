#ifndef R120_MEASURE_H
#define R120_MEASURE_H

#include <stdint.h>
#include "r120_signal.h"
#include "r120_czt.h"
#include "r120_range.h"
#include "r120_phase.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Software-only, single-dominant-target processing of ONE immutable ADC frame.
   Does not switch power, program the PLL, launch ADC/DMA or generate chirps.
   Call in foreground ONLY after R120_ACQ_STATUS_DONE and DMA has stopped.
   V0.8 is a baseline, NOT a calibrated high-accuracy level meter. */

typedef enum {
    R120_MEAS_OK = 0,
    R120_MEAS_BAD_ARGUMENT = 1,
    R120_MEAS_BAD_CONFIG = 2,
    R120_MEAS_BAD_SAMPLES = 3,
    R120_MEAS_CLIPPED = 4,
    R120_MEAS_NO_TARGET = 5,
    R120_MEAS_CZT_FAILED = 6,
    R120_MEAS_RANGE_FAILED = 7,
    R120_MEAS_LOW_QUALITY = 8,
    R120_MEAS_SELFTEST_FAILED = 9
} R120_MEAS_Status;

typedef struct {
    R120_RANGE_Config range;   /* Chirp parameters; ideally measured/calibrated. */
    uint32_t sample_rate_hz;  /* Actual sampling clock, initially 400000. */
    uint32_t sample_count;    /* Initially 2400 packed IQ samples. */
    float min_peak_contrast_db;   /* Spectrum peak-to-background, NOT SNR. */
    float min_amplitude_counts;
    float min_coherent_fraction;  /* 0..1, a single-tone consistency proxy. */
    float max_clipped_fraction;   /* 0..1, ADC reading near 0/4095. */
} R120_MEAS_Config;

typedef struct {
    R120_MEAS_Status status;
    R120_SIG_Status fft_status;
    R120_CZT_Status czt_status;
    R120_PHASE_Status phase_status;
    R120_RANGE_Status range_status;
    uint32_t clipped_samples;   /* Pairs with I or Q at ADC rail. */
    float clipped_fraction;
    uint8_t distance_valid;
    uint8_t phase_valid;
    R120_SIG_Result fft;
    R120_CZT_Result czt;
    R120_PHASE_Result phase;
    R120_RANGE_Result range;
} R120_MEAS_Result;

/* Sets conservative diagnostic gates; tune after measuring real IF noise. */
R120_MEAS_Status R120_MEAS_MakeConfig(const R120_RANGE_Config *range,
                                     uint32_t sample_rate_hz,
                                     uint32_t sample_count,
                                     R120_MEAS_Config *out);

/* Same input frame for FFT / CZT / phase. A GOOD measurement has status==OK,
   distance_valid=1 and phase_valid=1. If phase estimate fails but range works,
   return LOW_QUALITY and leave distance_valid=1; NEVER use phase in that case.
   Output retains partial diagnostics on errors. */
R120_MEAS_Status R120_MEAS_AnalyzePacked(const uint32_t *packed,
                                         uint32_t sample_count,
                                         const R120_MEAS_Config *cfg,
                                         R120_MEAS_Result *out);

/* CPU-only end-to-end synthetic ~5m test; no ADC or PLL operation. */
R120_MEAS_Status R120_MEAS_RunSelfTest(R120_MEAS_Result *out);

#ifdef __cplusplus
}
#endif

#endif /* R120_MEASURE_H */
