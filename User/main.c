/**
 ****************************************************************************************************
 * @file     main.c
 * @author   
 * @version  V1.0
 * @date     2026-11-16
 * @brief    雷达液位计-HAL库版本 
 * @license  
 */
 
#include "sys.h"
#include "usart.h"
#include "delay.h"
#include "rs485.h"
#include "sensor.h"
#include "tmp125.h"
#include "24lc32a.h"
#include "adf4159.h"
#include "oled.h"
#include "r120_acq.h"

float temp;
static uint8_t val; 

int main(void)
{
    HAL_Init();                                 /* 初始化HAL库 */
    sys_stm32_clock_init(432, 25, 2, 9);        /* 设置时钟, 216Mhz */
    delay_init(216);                            /* 延时初始化 */
    led_init();                                 /* LED初始化 */
    rs485_init(115200);                         /* RS485初始化 */
	  TMP125_Init();                              /* TMP125温度初始化 */
	  iic_init();                                 /* 24LC32A存储 IIC初始化 */
	  ADF4159_Init();                             /* ADF4159初始化 */
	  ADF4159_Config();                           /* ADF4159配置 */
	  OLED_Init();			                          /* 初始化屏OLED */ 
	
    while(1)
    {  
			LED(1);                            /* PA11电平拉高LED0灭 */
			delay_ms(500);
			LED(0);                            /* PA11电平拉低LED0亮 */
			delay_ms(500);
		
			temp = TMP125_ReadTemperature();
			/* 单字节写测试，地址0x10 */
			//EEPROM_WriteByte(0x10, 0x66);
			val=EEPRO_ReadByte(0x10);
		
			OLED_Update();	
		
			measure();
    }
}

