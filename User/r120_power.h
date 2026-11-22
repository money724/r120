#ifndef R120_POWER_H
#define R120_POWER_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* Debug stage: 0=off, 1=AN_3V3 on, 2=ADF_1V8 on,
 * 3=75MHz clock enabled, 4=TRX divider & normal TX power,
 * 5=ADF4159 CE asserted. These values are SOFTWARE states,
 * NOT measured rail voltages or verified PLL-lock states.
 */
extern volatile uint32_t r120_power_stage;

/* Call AFTER MX_GPIO_Init(). Always drives enable pins LOW. */
void R120_PowerSafeOff(void);

/* One call advances exactly ONE stage. For supervised bring-up ONLY.
 * Do not call repeatedly in while(1) during the first hardware test.
 */
HAL_StatusTypeDef R120_PowerAdvance(void);

#endif /* R120_POWER_H */
