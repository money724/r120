#include "./SYSTEM/sys/sys.h"
#include "sensor.h"

/**
 * @brief       ��0ʼ��LED���IO��, ��ʹ��ʱ��
 * @param       ��
 * @retval      ��
 */
void led_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;

    LED_GPIO_CLK_ENABLE();                                    /* GPIOA ʱ��ʹ�� */
    
    gpio_init_struct.Pin = LED_GPIO_PIN;                         /* LED0���� */
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;               /* ������� */
    gpio_init_struct.Pull = GPIO_PULLUP;                       /* ���� */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;             /* ���� */
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio_init_struct);          /* ��ʼ��LED0���� */
}
