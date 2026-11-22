#ifndef R120_ADF4159_CONFIG_H
#define R120_ADF4159_CONFIG_H

#include <stdint.h>
#include "stm32f7xx_hal.h"

/*
 * R120 / STM32F765 / TRX_120_001 / ADF4159
 * Register planning is software-only. The default is NO hardware writes.
 * Change to 1 ONLY after the board supply and reference clock are verified.
 */
#ifndef ADF4159_CFG_ALLOW_HW_WRITES
#define ADF4159_CFG_ALLOW_HW_WRITES  0U
#endif

#define ADF4159_CFG_INIT_WORD_COUNT  11U
#define ADF4159_CFG_REF_HZ           UINT64_C(75000000)
#define ADF4159_CFG_TRX_DIVIDER      UINT64_C(64)

/* Initial conservative SOFTWARE planning envelope; does not guarantee lock. */
#define ADF4159_CFG_MIN_RF_HZ        UINT64_C(120000000000)
#define ADF4159_CFG_MAX_RF_HZ        UINT64_C(125000000000)

typedef enum {
    ADF4159_CFG_FIXED = 1,
    ADF4159_CFG_SINGLE_SAWTOOTH = 2
} ADF4159_CFG_Mode;

typedef enum {
    ADF4159_CFG_OK = 0,
    ADF4159_CFG_BAD_ARG = 1,
    ADF4159_CFG_UNREPRESENTABLE = 2,
    ADF4159_CFG_NOT_READY = 3,
    ADF4159_CFG_WRITE_FAILED = 4
} ADF4159_CFG_Status;

typedef struct {
    uint32_t valid_tag;                   /* populated only on successful plan */
    ADF4159_CFG_Mode mode;
    uint64_t requested_start_hz;
    uint64_t requested_stop_hz;
    uint64_t actual_start_hz;             /* rounded to 25-bit fractional grid */
    uint64_t actual_stop_hz;              /* estimated digital sweep endpoint */
    uint64_t actual_bandwidth_hz;         /* rounded to nearest Hz */
    uint32_t requested_duration_us;
    uint32_t actual_duration_us;
    uint32_t steps;                      /* R6 20-bit count */
    uint16_t clk1;                       /* R2 = 180 */
    uint16_t clk2;                       /* R4 = 1 */
    uint16_t deviation;                  /* R5 signed deviation, positive here */
    uint8_t deviation_offset;            /* 2^offset */
    uint32_t words[ADF4159_CFG_INIT_WORD_COUNT];
    uint32_t r0_ramp_off;
    uint32_t r0_ramp_on;
} ADF4159_CFG_Plan;

/* These functions NEVER write to the ADF4159 and are safe to run without a board. */
ADF4159_CFG_Status ADF4159_CFG_PlanFixedHz(uint64_t rf_hz, ADF4159_CFG_Plan *out);
ADF4159_CFG_Status ADF4159_CFG_PlanRampHz(uint64_t start_hz, uint64_t stop_hz,
                                          uint32_t duration_us, ADF4159_CFG_Plan *out);

/* Hardware access, compiled out by default. Requires power stage 5 and ADF_CE high. */
HAL_StatusTypeDef ADF4159_CFG_Apply(const ADF4159_CFG_Plan *plan);
HAL_StatusTypeDef ADF4159_CFG_StartRamp(void);
HAL_StatusTypeDef ADF4159_CFG_StopRamp(void);

/* Debug observability: software state only, not readback or confirmation of lock. */
extern volatile uint32_t adf4159_cfg_last_status;
extern volatile uint32_t adf4159_cfg_last_r0;
extern volatile uint32_t adf4159_cfg_applied_mode;

#endif
