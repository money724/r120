#include "./SYSTEM/sys/sys.h"
#include "sensor.h"

/**
 * @brief       初0始化LED相关IO口, 并使能时钟
 * @param       无
 * @retval      无
 */
void led_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;

    LED_GPIO_CLK_ENABLE();                                    /* GPIOA 时钟使能 */
    
    gpio_init_struct.Pin = LED_GPIO_PIN;                         /* LED0引脚 */
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;               /* 推挽输出 */
    gpio_init_struct.Pull = GPIO_PULLUP;                       /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;             /* 高速 */
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio_init_struct);          /* 初始化LED0引脚 */
}
