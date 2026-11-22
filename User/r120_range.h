#ifndef R120_RANGE_H
#define R120_RANGE_H

#include <stdint.h>
#include "r120_signal.h"
#include "adf4159_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Monostatic FMCW, stationary single dominant target, linear ramp assumption.
 * This module DOES NOT control PLL, ADC, DMA, TIM or power GPIO.
 * It estimates antenna-to-target slant distance, NOT tank liquid height.
 */
#define R120_RANGE_LIGHT_SPEED_MPS 299792458.0

typedef enum
{
    R120_RANGE_OK = 0,
    R120_RANGE_BAD_ARGUMENT = 1,
    R120_RANGE_BAD_SWEEP = 2,
    R120_RANGE_NO_VALID_PEAK = 3,
    R120_RANGE_OUT_OF_RANGE = 4,
    R120_RANGE_SELFTEST_FAILED = 5
} R120_RANGE_Status;

typedef struct
{
    uint64_t bandwidth_hz;       /* Prefer actual_bandwidth_hz from ADF plan. */
    uint32_t duration_us;        /* Prefer actual_duration_us from ADF plan. */
    double min_distance_m;       /* Inclusive result filter. */
    double max_distance_m;       /* Inclusive result filter. */
    double distance_offset_m;    /* Added to raw distance; initially 0. */
} R120_RANGE_Config;

typedef struct
{
    float signed_beat_hz;        /* Preserve sign for debugging I/Q polarity. */
    float abs_beat_hz;           /* Absolute beat used for distance. */
    double slope_hz_per_s;
    double raw_distance_m;      /* Before calibration offset. */
    double distance_m;          /* After offset; NOT liquid level. */
} R120_RANGE_Result;

/* Only math and input validation; performs no hardware accesses. */
R120_RANGE_Status R120_RANGE_MakeConfig(uint64_t bandwidth_hz,
                                        uint32_t duration_us,
                                        double min_distance_m,
                                        double max_distance_m,
                                        double distance_offset_m,
                                        R120_RANGE_Config *out);

/* Use actual digital values from a VALID positive-slope ADF ramp plan.
 * This is still a digital PLANNING model, not a measurement of real chirp.
 */
R120_RANGE_Status R120_RANGE_ConfigFromPlan(const ADF4159_CFG_Plan *plan,
                                            double min_distance_m,
                                            double max_distance_m,
                                            double distance_offset_m,
                                            R120_RANGE_Config *out);

/* Distance from an estimated beat, positive OR negative. */
R120_RANGE_Status R120_RANGE_FromBeatHz(float signed_beat_hz,
                                        const R120_RANGE_Config *cfg,
                                        R120_RANGE_Result *out);

/* Require a successful V0.4 signal estimator before calculating range. */
R120_RANGE_Status R120_RANGE_FromSignal(R120_SIG_Status signal_status,
                                        const R120_SIG_Result *signal,
                                        const R120_RANGE_Config *cfg,
                                        R120_RANGE_Result *out);

/* Ideal beat for one simulated distance (calibration offset not applied). */
R120_RANGE_Status R120_RANGE_ExpectedBeatHz(double raw_distance_m,
                                            const R120_RANGE_Config *cfg,
                                            double *out_beat_hz);

/* Pure software: runs V0.4 synthetic IQ FFT and then distance conversion.
 * No PLL/ADC/DMA/GPIO calls. Valid result should be ~4.997 m, not exact 5m.
 */
R120_RANGE_Status R120_RANGE_RunSelfTest(R120_RANGE_Result *out);

#ifdef __cplusplus
}
#endif

#endif /* R120_RANGE_H */
