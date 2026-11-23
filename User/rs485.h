/**
 ****************************************************************************************************
 * @file        rs485.h
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-07-19
 * @brief       RS485 ��������
 * @license     Copyright (c) 2020-2032, ������������ӿƼ����޹�˾
 ****************************************************************************************************
 */

#ifndef __RS485_H
#define __RS485_H

#include "./SYSTEM/sys/sys.h"


/******************************************************************************************/
/* RS485 ���� �� ���� ���� 
 * Ĭ�������RS485��.
 * ע��: ͨ���޸���10���궨��, ����֧��UART1~UART7����һ������.
 */
#define RS485_TX_GPIO_PORT                  GPIOA
#define RS485_TX_GPIO_PIN                   GPIO_PIN_15
#define RS485_TX_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)   /* PA��ʱ��ʹ�� */

#define RS485_RX_GPIO_PORT                  GPIOB
#define RS485_RX_GPIO_PIN                   GPIO_PIN_3
#define RS485_RX_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB��ʱ��ʹ�� */

#define RS485_UX                            UART7
#define RS485_UX_IRQn                       UART7_IRQn
#define RS485_UX_IRQHandler                 UART7_IRQHandler
#define RS485_UX_CLK_ENABLE()               __HAL_RCC_UART7_CLK_ENABLE()    /* USART7 ʱ��ʹ�� */

#define RE_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0) /* PB��ʱ��ʹ�� */

/******************************************************************************************/

/* ����봮���жϽ��գ�����EN_USART7_RXΪ1����������Ϊ0 */
#define EN_USART7_RX               1                /* 0,������;1,����. */
#define RS485_REC_LEN              64               /* �����������ֽ��� 64 */

extern uint8_t g_RS485_rx_buf[RS485_REC_LEN];       /* ���ջ���,���RS485_REC_LEN���ֽ� */
extern uint8_t g_RS485_rx_cnt;                      /* ���յ������ݳ��� */

void rs485_init(uint32_t bound);
void rs485_send_data(uint8_t *buf, uint8_t len);
void rs485_receive_data(uint8_t *buf, uint8_t *len);
void rs485_tx_set(uint8_t en);

#endif
