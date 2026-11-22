#include "adf4159_config.h"
#include "adf4159.h"       /* Transport-only V0.1: ADF4159_WriteWord */
#include "r120_power.h"    /* r120_power_stage == 5 ONLY means software stage */
#include <string.h>

/* ADF4159 Rev.D data sheet register map, fractional modulus = 2^25.
 * 75 MHz REFIN; doubler off; reference counter 1; RDIV2 off.
 * R2 CP current = 5.0 mA is a PROVISIONAL test assumption, NOT board-validated.
 */
#define CFG_MODULUS            UINT64_C(33554432)
#define CFG_REF_TIMES_DIV      UINT64_C(4800000000) /* 75 MHz * 64 */
#define CFG_CLK1               180U
#define CFG_CLK2               1U
#define CFG_R0_MUX_DIG_LOCK    (6UL << 27)
#define CFG_R0_MUX_RAMP_DONE   (15UL << 27)
#define CFG_R0_RAMP_ON         (1UL << 31)
#define CFG_R3_FIXED           0x000380C3UL
#define CFG_R3_SINGLE_SAW      0x000388C3UL
#define CFG_R2_75MHZ           0x0F0085A2UL
#define CFG_R4_FIXED_CLK1      0x00000084UL
#define CFG_R4_FIXED_CLK2      0x000000C4UL
#define CFG_R4_RAMP_CLK1       0x00780084UL
#define CFG_R4_RAMP_CLK2       0x007800C4UL
#define CFG_VALID_TAG          0x415DF159UL

volatile uint32_t adf4159_cfg_last_status = ADF4159_CFG_OK;
volatile uint32_t adf4159_cfg_last_r0 = 0U;
volatile uint32_t adf4159_cfg_applied_mode = 0U;

static uint32_t s_r0_off = 0U;
static uint32_t s_r0_on = 0U;
static uint8_t s_ramp_running = 0U;

static ADF4159_CFG_Status finish_status(ADF4159_CFG_Status s)
{
    adf4159_cfg_last_status = (uint32_t)s;
    return s;
}

/* Calculate INT and the two FRAC portions, round to closest RF output step.
 * rf_hz * modulus would overflow 64 bits, so use quotient and remainder.
 */
static ADF4159_CFG_Status make_frequency_words(uint64_t rf_hz,
                                                 uint32_t mux_bits,
                                                 uint32_t *r0,
                                                 uint32_t *r1,
                                                 uint64_t *actual_rf_hz)
{
    uint64_t integer_n;
    uint64_t remainder;
    uint64_t frac;
    uint64_t effective_num;

    if (rf_hz < ADF4159_CFG_MIN_RF_HZ || rf_hz > ADF4159_CFG_MAX_RF_HZ ||
        r0 == NULL || r1 == NULL || actual_rf_hz == NULL) {
        return ADF4159_CFG_BAD_ARG;
    }

    integer_n = rf_hz / CFG_REF_TIMES_DIV;
    remainder = rf_hz % CFG_REF_TIMES_DIV;
    frac = (remainder * CFG_MODULUS + CFG_REF_TIMES_DIV / 2U) / CFG_REF_TIMES_DIV;
    if (frac >= CFG_MODULUS) {
        integer_n += 1U;
        frac = 0U;
    }
    /* R2 prescaler=4/5 -> minimum INT=23. */
    if (integer_n < 23U || integer_n > 4095U) {
        return ADF4159_CFG_UNREPRESENTABLE;
    }

    *r1 = ((uint32_t)(frac & 0x1FFFUL) << 15) | 0x1U;
    *r0 = mux_bits | ((uint32_t)integer_n << 15) |
          ((uint32_t)(frac >> 13) << 3);

    /* actual frequency rounded to nearest Hz, split for safe 64-bit math */
    effective_num = frac * CFG_REF_TIMES_DIV;
    *actual_rf_hz = integer_n * CFG_REF_TIMES_DIV +
                    (effective_num + CFG_MODULUS / 2U) / CFG_MODULUS;
    return ADF4159_CFG_OK;
}

static void fill_common(ADF4159_CFG_Plan *p)
{
    p->words[0] = 0x00000007UL;     /* R7, TXDATA-related triggers off */
    p->words[2] = 0x00800006UL;     /* R6, second step word (zero) */
    p->words[4] = 0x00800005UL;     /* R5, second DEV word (zero) */
    p->words[8] = CFG_R2_75MHZ;
    p->clk1 = CFG_CLK1;
    p->clk2 = CFG_CLK2;
}

ADF4159_CFG_Status ADF4159_CFG_PlanFixedHz(uint64_t rf_hz,ADF4159_CFG_Plan *out)
{
    uint32_t r0, r1;
    uint64_t actual;
    ADF4159_CFG_Status s;
    if (out == NULL) return finish_status(ADF4159_CFG_BAD_ARG);
    memset(out, 0, sizeof(*out));

    s = make_frequency_words(rf_hz, CFG_R0_MUX_DIG_LOCK, &r0, &r1, &actual);
    if (s != ADF4159_CFG_OK) return finish_status(s);

    fill_common(out);
    out->words[1] = 0x00000006UL;     /* R6 step1 unused in fixed mode */
    out->words[3] = 0x00000005UL;     /* R5 DEV1 unused in fixed mode */
    out->words[5] = CFG_R4_FIXED_CLK1;
    out->words[6] = CFG_R4_FIXED_CLK2;
    out->words[7] = CFG_R3_FIXED;
    out->words[9] = r1;
    out->words[10] = r0;
    out->r0_ramp_off = r0;
    out->r0_ramp_on = r0;              /* fixed mode never enables ramp */
    out->mode = ADF4159_CFG_FIXED;
    out->requested_start_hz = rf_hz;
    out->requested_stop_hz = rf_hz;
    out->actual_start_hz = actual;
    out->actual_stop_hz = actual;
    out->valid_tag = CFG_VALID_TAG;
    return finish_status(ADF4159_CFG_OK);
}

/* Unsigned rational step calculation without floating point or 128-bit ints.
 * The returned bandwidth is rounded to integer Hz.
 */
static uint64_t actual_bandwidth(uint32_t dev, uint32_t offset, uint32_t steps)
{
    const uint64_t k = (uint64_t)dev * (UINT64_C(1) << offset) * steps;
    const uint64_t q = k / CFG_MODULUS;
    const uint64_t r = k % CFG_MODULUS;
    return q * CFG_REF_TIMES_DIV +
           (r * CFG_REF_TIMES_DIV + CFG_MODULUS / 2U) / CFG_MODULUS;
}

ADF4159_CFG_Status ADF4159_CFG_PlanRampHz(uint64_t start_hz,
                                           uint64_t stop_hz,
                                           uint32_t duration_us,
                                           ADF4159_CFG_Plan *out)
{
    uint32_t r0, r1;
    uint32_t steps;
    uint32_t dev = 0U;
    uint32_t offset;
    uint32_t chosen_offset = 0U;
    uint64_t real_start;
    uint64_t num, denominator;
    uint64_t bw, real_bw;
    ADF4159_CFG_Status s;

    if (out == NULL) return finish_status(ADF4159_CFG_BAD_ARG);
    memset(out, 0, sizeof(*out));

    /* Only positive-slope, single sawtooth is supported in this first revision.
     * Guard bounds keep every intermediary within 64-bit arithmetic limits. */
    if (start_hz < ADF4159_CFG_MIN_RF_HZ ||
        stop_hz > ADF4159_CFG_MAX_RF_HZ ||
        stop_hz <= start_hz ||
        duration_us < 1000U || duration_us > 100000U) {
        return finish_status(ADF4159_CFG_BAD_ARG);
    }
    steps = (uint32_t)(((uint64_t)duration_us * 75U + CFG_CLK1 / 2U) / CFG_CLK1);
    if (steps < 2U || steps > 0xFFFFFU) return finish_status(ADF4159_CFG_UNREPRESENTABLE);

    bw = stop_hz - start_hz;
    num = bw * CFG_MODULUS;                /* <= 1.68e17, safe */
    denominator = CFG_REF_TIMES_DIV * (uint64_t)steps;
    /* Try the finest possible deviation scale before accepting coarser steps. */
    for (offset = 0U; offset <= 15U; ++offset) {
        const uint64_t scaled = denominator * (UINT64_C(1) << offset);
        const uint64_t candidate = (num + scaled / 2U) / scaled;
        if (candidate >= 1U && candidate <= 32767U) {
            dev = (uint32_t)candidate;
            chosen_offset = offset;
            break;
        }
    }
    if (dev == 0U) return finish_status(ADF4159_CFG_UNREPRESENTABLE);

    s = make_frequency_words(start_hz, CFG_R0_MUX_RAMP_DONE, &r0, &r1, &real_start);
    if (s != ADF4159_CFG_OK) return finish_status(s);

    real_bw = actual_bandwidth(dev, chosen_offset, steps);
    if (real_start + real_bw > ADF4159_CFG_MAX_RF_HZ) {
        return finish_status(ADF4159_CFG_UNREPRESENTABLE);
    }

    fill_common(out);
    out->words[1] = ((steps & 0xFFFFFUL) << 3) | 0x6U;
    out->words[3] = (chosen_offset << 19) | (dev << 3) | 0x5U;
    out->words[5] = CFG_R4_RAMP_CLK1;
    out->words[6] = CFG_R4_RAMP_CLK2;
    out->words[7] = CFG_R3_SINGLE_SAW;
    out->words[9] = r1;
    out->words[10] = r0;             /* R0 DB31=0: merely prepare ramp */
    out->r0_ramp_off = r0;
    out->r0_ramp_on = r0 | CFG_R0_RAMP_ON;
    out->mode = ADF4159_CFG_SINGLE_SAWTOOTH;
    out->requested_start_hz = start_hz;
    out->requested_stop_hz = stop_hz;
    out->actual_start_hz = real_start;
    out->actual_bandwidth_hz = real_bw;
    out->actual_stop_hz = real_start + real_bw;
    out->requested_duration_us = duration_us;
    out->actual_duration_us = (uint32_t)(((uint64_t)steps * CFG_CLK1 + 37U) / 75U);
    out->steps = steps;
    out->deviation = (uint16_t)dev;
    out->deviation_offset = (uint8_t)chosen_offset;
    out->valid_tag = CFG_VALID_TAG;
    return finish_status(ADF4159_CFG_OK);
}

static HAL_StatusTypeDef hw_ready(void)
{
#if ADF4159_CFG_ALLOW_HW_WRITES
    /* Important: this is not a measurement of the actual supply rail. */
    if (r120_power_stage != 5U ||
        HAL_GPIO_ReadPin(ADF_CE_GPIO_Port, ADF_CE_Pin) != GPIO_PIN_SET) {
        adf4159_cfg_last_status = ADF4159_CFG_NOT_READY;
        return HAL_ERROR;
    }
    return HAL_OK;
#else
    adf4159_cfg_last_status = ADF4159_CFG_NOT_READY;
    return HAL_ERROR;
#endif
}

HAL_StatusTypeDef ADF4159_CFG_Apply(const ADF4159_CFG_Plan *plan)
{
    uint32_t i;
    if (plan == NULL || plan->valid_tag != CFG_VALID_TAG || hw_ready() != HAL_OK) {
        return HAL_ERROR;
    }
    /* Invalidate any older state before attempting a new register sequence. */
    adf4159_cfg_applied_mode = 0U;
    s_ramp_running = 0U;
    for (i = 0U; i < ADF4159_CFG_INIT_WORD_COUNT; ++i) {
        if (ADF4159_WriteWord(plan->words[i]) != HAL_OK) {
            adf4159_cfg_last_status = ADF4159_CFG_WRITE_FAILED;
            return HAL_ERROR;
        }
    }
    s_r0_off = plan->r0_ramp_off;
    s_r0_on = plan->r0_ramp_on;
    s_ramp_running = 0U;
    adf4159_cfg_applied_mode = (uint32_t)plan->mode;
    adf4159_cfg_last_r0 = s_r0_off;
    adf4159_cfg_last_status = ADF4159_CFG_OK;
    return HAL_OK;
}

HAL_StatusTypeDef ADF4159_CFG_StartRamp(void)
{
    if (hw_ready() != HAL_OK ||
        adf4159_cfg_applied_mode != ADF4159_CFG_SINGLE_SAWTOOTH ||
        s_ramp_running != 0U) {
        return HAL_ERROR;
    }
    if (ADF4159_WriteWord(s_r0_on) != HAL_OK) {
        adf4159_cfg_last_status = ADF4159_CFG_WRITE_FAILED;
        return HAL_ERROR;
    }
    adf4159_cfg_last_r0 = s_r0_on;
    s_ramp_running = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef ADF4159_CFG_StopRamp(void)
{
    if (hw_ready() != HAL_OK ||
        adf4159_cfg_applied_mode != ADF4159_CFG_SINGLE_SAWTOOTH) {
        return HAL_ERROR;
    }
    if (ADF4159_WriteWord(s_r0_off) != HAL_OK) {
        adf4159_cfg_last_status = ADF4159_CFG_WRITE_FAILED;
        return HAL_ERROR;
    }
    adf4159_cfg_last_r0 = s_r0_off;
    s_ramp_running = 0U;
    return HAL_OK;
}
