/**
 ****************************************************************************************************
 * @file        rs485.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-07-19
 * @brief       RS485 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 */

#ifndef __RS485_H
#define __RS485_H

#include "./SYSTEM/sys/sys.h"


/******************************************************************************************/
/* RS485 引脚 和 串口 定义 
 * 默认是针对RS485的.
 * 注意: 通过修改这10个宏定义, 可以支持UART1~UART7任意一个串口.
 */
#define RS485_TX_GPIO_PORT                  GPIOA
#define RS485_TX_GPIO_PIN                   GPIO_PIN_15
#define RS485_TX_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)   /* PA口时钟使能 */

#define RS485_RX_GPIO_PORT                  GPIOB
#define RS485_RX_GPIO_PIN                   GPIO_PIN_3
#define RS485_RX_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB口时钟使能 */

#define RS485_UX                            UART7
#define RS485_UX_IRQn                       UART7_IRQn
#define RS485_UX_IRQHandler                 UART7_IRQHandler
#define RS485_UX_CLK_ENABLE()               __HAL_RCC_UART7_CLK_ENABLE()    /* USART7 时钟使能 */

#define RE_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0) /* PB口时钟使能 */

/******************************************************************************************/

/* 如果想串口中断接收，设置EN_USART7_RX为1，否则设置为0 */
#define EN_USART7_RX               1                /* 0,不接收;1,接收. */
#define RS485_REC_LEN              64               /* 定义最大接收字节数 64 */

extern uint8_t g_RS485_rx_buf[RS485_REC_LEN];       /* 接收缓冲,最大RS485_REC_LEN个字节 */
extern uint8_t g_RS485_rx_cnt;                      /* 接收到的数据长度 */

void rs485_init(uint32_t bound);
void rs485_send_data(uint8_t *buf, uint8_t len);
void rs485_receive_data(uint8_t *buf, uint8_t *len);
void rs485_tx_set(uint8_t en);

#endif
