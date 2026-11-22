#ifndef _SENSOR_H
#define _SENSOR_H

/******************************************************************************************/
/* 引脚 定义 */

#define LED_GPIO_PORT               GPIOA
#define LED_GPIO_PIN                GPIO_PIN_11
#define LED_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)   /* PA口时钟使能 */

/******************************************************************************************/

/* IO操作 */
#define LED(x)        do{ x ? \
                              HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_RESET); \
                          }while(0)       /* LED */

void led_init(void);
#endif
													
													