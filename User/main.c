/**
 ****************************************************************************************************
 * @file     main.c
 * @author   
 * @version  V1.0
 * @date     2026-11-16
 * @brief    �״�Һλ��-HAL��汾 
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
    HAL_Init();                                 /* ��ʼ��HAL�� */
    sys_stm32_clock_init(432, 25, 2, 9);        /* ����ʱ��, 216Mhz */
    delay_init(216);                            /* ��ʱ��ʼ�� */
    led_init();                                 /* LED��ʼ�� */
    rs485_init(115200);                         /* RS485��ʼ�� */
	  TMP125_Init();                              /* TMP125�¶ȳ�ʼ�� */
	  iic_init();                                 /* 24LC32A�洢 IIC��ʼ�� */
	  ADF4159_Init();                             /* ADF4159��ʼ�� */
	  ADF4159_Config();                           /* ADF4159���� */
	  OLED_Init();			                          /* ��ʼ����OLED */ 
	
    while(1)
    {  
			LED(1);                            /* PA11��ƽ����LED0�� */
			delay_ms(500);
			LED(0);                            /* PA11��ƽ����LED0�� */
			delay_ms(500);
		
			temp = TMP125_ReadTemperature();
			/* ���ֽ�д���ԣ���ַ0x10 */
			//EEPROM_WriteByte(0x10, 0x66);
			val=EEPRO_ReadByte(0x10);
		
			OLED_Update();	
		
			measure();
    }
}

