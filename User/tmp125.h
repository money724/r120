#ifndef __TMP125_H
#define __TMP125_H

#include "stm32f7xx_hal.h"

/* 引脚定义 */
#define TMP125_SCK_PIN     GPIO_PIN_2
#define TMP125_SCK_PORT    GPIOE

#define TMP125_CS_PIN      GPIO_PIN_4
#define TMP125_CS_PORT     GPIOE

#define TMP125_SI_PIN      GPIO_PIN_6
#define TMP125_SI_PORT     GPIOE

#define TMP125_SO_PIN      GPIO_PIN_5
#define TMP125_SO_PORT     GPIOE

void TMP125_Init(void);
int16_t TMP125_ReadRaw(void);
float TMP125_ReadTemperature(void);
void TMP125_EnterShutdown(void);
void TMP125_ExitShutdown(void);

#endif
