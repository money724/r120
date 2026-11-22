#include "r120_power.h"
#include "adf4159.h"

volatile uint32_t r120_power_stage = 0U;

void R120_PowerSafeOff(void)
{
    /* Disable PLL first, then divider/TX power control, oscillator,
       1.8V regulator, and finally analog 3.3V supply. */
    HAL_GPIO_WritePin(ADF_CE_GPIO_Port,     ADF_CE_Pin,     GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TRX_REN_GPIO_Port,    TRX_REN_Pin,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ADF_CLK_EN_GPIO_Port, ADF_CLK_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ADF18_EN_GPIO_Port,   ADF18_EN_Pin,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AN3V3_EN_GPIO_Port,   AN3V3_EN_Pin,   GPIO_PIN_RESET);
    r120_power_stage = 0U;
}

HAL_StatusTypeDef R120_PowerAdvance(void)
{
    /* Caution: this function DOES NOT check power rails. After EACH stage,
       use a voltmeter/oscilloscope to verify the expected actual voltage. */
    switch (r120_power_stage)
    {
    case 0U:
        HAL_GPIO_WritePin(AN3V3_EN_GPIO_Port, AN3V3_EN_Pin, GPIO_PIN_SET);
        HAL_Delay(10U);                 /* initial conservative settling margin */
        r120_power_stage = 1U;
        return HAL_OK;
    case 1U:
        HAL_GPIO_WritePin(ADF18_EN_GPIO_Port, ADF18_EN_Pin, GPIO_PIN_SET);
        HAL_Delay(10U);
        r120_power_stage = 2U;
        return HAL_OK;
    case 2U:
        HAL_GPIO_WritePin(ADF_CLK_EN_GPIO_Port, ADF_CLK_EN_Pin, GPIO_PIN_SET);
        HAL_Delay(10U);                 /* ASDMB startup max ~3 ms per datasheet */
        r120_power_stage = 3U;
        return HAL_OK;
    case 3U:
        /* High -> ~1.2V at diven/pwr_tx via R10/R11 divider.
           This ENABLES divider and sets normal TX power. */
        HAL_GPIO_WritePin(TRX_REN_GPIO_Port, TRX_REN_Pin, GPIO_PIN_SET);
        HAL_Delay(1U);
        r120_power_stage = 4U;
        return HAL_OK;
    case 4U:
        HAL_GPIO_WritePin(ADF_CE_GPIO_Port, ADF_CE_Pin, GPIO_PIN_SET);
        HAL_Delay(1U);
        r120_power_stage = 5U;
        return HAL_OK;
    default:
        return HAL_ERROR;
    }
}
