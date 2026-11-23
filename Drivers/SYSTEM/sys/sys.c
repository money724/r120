/**
 ****************************************************************************************************
 * @file        sys.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-07-19
 * @brief       ϵͳ��ʼ������(����ʱ������/�жϹ���/GPIO���õ�)
 * @license     Copyright (c) 2020-2032, ������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� ������ F767������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 * �޸�˵��
 * V1.0 20220719
 * ��һ�η���
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"

/**
 * @brief       ִ��: WFIָ��(ִ�����ָ�����͹���״̬, �ȴ��жϻ���)
 * @param       ��
 * @retval      ��
 */
void sys_wfi_set(void)
{
    __ASM volatile("wfi");
}

/**
 * @brief       �ر������ж�(���ǲ�����fault��NMI�ж�)
 * @param       ��
 * @retval      ��
 */
void sys_intx_disable(void)
{
    __ASM volatile("cpsid i");
}

/**
 * @brief       ���������ж�
 * @param       ��
 * @retval      ��
 */
void sys_intx_enable(void)
{
    __ASM volatile("cpsie i");
}

/**
 * @brief       ����ջ����ַ
 * @note        ���ĺ�X, ����MDK��, ʵ����û�����
 * @param       addr: ջ����ַ
 * @retval      ��
 */
void sys_msr_msp(uint32_t addr)
{
    __set_MSP(addr);    /* ����ջ����ַ */
}

/**
 * @brief       ʹ��STM32F7��L1-Cache, ͬʱ����D cache��ǿ��͸д
 * @param       ��
 * @retval      ��
 */
void sys_cache_enable(void)
{
    SCB_EnableICache(); /* ʹ��I-Cache,������core_cm7.h���涨�� */
    SCB_EnableDCache(); /* ʹ��D-Cache,������core_cm7.h���涨�� */
    SCB->CACR |= 1 << 2;/* ǿ��D-Cache͸д,�粻����͸д,ʵ��ʹ���п��������������� */
}

/**
 * @brief       �ж�I_Cache�Ƿ��
 * @param       ��
 * @retval      ����ֵ:0 �رգ�1 ��
 */
uint8_t get_icache_sta(void)
{
    uint8_t sta;
    sta = ((SCB->CCR) >> 17) & 0X01;
    return sta;
}

/**
 * @brief       �ж�D_Cache�Ƿ��
 * @param       ��
 * @retval      ����ֵ:0 �رգ�1 ��
 */
uint8_t get_dcache_sta(void)
{
    uint8_t sta;
    sta = ((SCB->CCR) >> 16) & 0X01;
    return sta;
}

/**
 * @brief       ʱ�����ú���
 * @param       plln: PLL1��Ƶϵ��(PLL��Ƶ), ȡֵ��Χ: 192~432.
 * @param       pllm: PLL1Ԥ��Ƶϵ��(��PLL֮ǰ�ķ�Ƶ), ȡֵ��Χ: 2~63.
 * @param       pllp: PLL1��p��Ƶϵ��(PLL֮��ķ�Ƶ), ��Ƶ����Ϊϵͳʱ��, ȡֵ��Χ: 2,4,6,8.(������4��ֵ!)
 * @param       pllq: PLL1��q��Ƶϵ��(PLL֮��ķ�Ƶ), ȡֵ��Χ: 2~15.
 * @note
 *
 *              Fvco: VCOƵ��
 *              Fsys: ϵͳʱ��Ƶ��, Ҳ��PLL1��p��Ƶ���ʱ��Ƶ��
 *              Fq:   PLL1��q��Ƶ���ʱ��Ƶ��
 *              Fs:   PLL����ʱ��Ƶ��, ������HSI, CSI, HSE��.
 *              Fvco = Fs * (plln / pllm);
 *              Fsys = Fvco / pllp = Fs * (plln / (pllm * pllp));
 *              Fq   = Fvco / pllq = Fs * (plln / (pllm * pllq));
 *
 *              �ⲿ����Ϊ25M��ʱ��, �Ƽ�ֵ: plln = 432, pllm = 25, pllp = 2, pllq = 9.
 *              �õ�:Fvco = 25 * ( 432 / 25) = 432Mhz
 *                   Fsys = pll1_p_ck = 432 / 2 = 216Mhz
 *                   Fq   = pll1_q_ck = 432 / 9 = 48Mhz
 *
 *              F767Ĭ����Ҫ���õ�Ƶ������:
 *              CPUƵ��(rcc_c_ck) = sys_d1cpre_ck = 216Mhz
 *              AHB1/2/3(rcc_hclk1/2/3) = 216Mhz
 *              APB1(rcc_pclk1) = pll_p_ck / 4 = 54Mhz
 *              APB2(rcc_pclk2) = pll_p_ck / 2 = 108Mhz
 *
 * @retval      �������: 0, �ɹ�; 1, ����;
 */
uint8_t sys_stm32_clock_init(uint32_t plln, uint32_t pllm, uint32_t pllp, uint32_t pllq)
{
    HAL_StatusTypeDef ret = HAL_OK;
    RCC_ClkInitTypeDef rcc_clk_init_handle;
    RCC_OscInitTypeDef rcc_osc_init_handle;
  
    __HAL_RCC_PWR_CLK_ENABLE();                                            /* ʹ��PWRʱ�� */

    /* ������������������õ�ѹ�������ѹ�����Ա�������δ�����Ƶ�ʹ���ʱʹ�����빦��ʵ��ƽ�� */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);         /* VOS = 1, Scale1, 1.2V�ں˵�ѹ,FLASH���ʿ��Եõ�������� */

    /* ʹ��HSE����ѡ��HSE��ΪPLLʱ��Դ������PLL1������USBʱ�� */
    rcc_osc_init_handle.OscillatorType = RCC_OSCILLATORTYPE_HSE;           /* ʱ��ԴΪHSE */
    rcc_osc_init_handle.HSEState = RCC_HSE_ON;                             /* ��HSE */
    rcc_osc_init_handle.PLL.PLLState = RCC_PLL_ON;                         /* ��PLL */
    rcc_osc_init_handle.PLL.PLLSource = RCC_PLLSOURCE_HSE;                 /* PLLʱ��Դѡ��HSE */
    rcc_osc_init_handle.PLL.PLLN = plln;
    rcc_osc_init_handle.PLL.PLLM = pllm;
    rcc_osc_init_handle.PLL.PLLP = pllp;
    rcc_osc_init_handle.PLL.PLLQ = pllq;
    ret = HAL_RCC_OscConfig(&rcc_osc_init_handle);
    if(ret != HAL_OK)
    {
        return 1;
    }
    
    ret = HAL_PWREx_EnableOverDrive();   /* ����Over-Driver���� */
    if(ret != HAL_OK)
    {
        return 1;
    }

    /*
     *  ѡ��PLL�������Ϊϵͳʱ��
     *  ����RCC_CLOCKTYPE_SYSCLKϵͳʱ��,216M
     *  ����RCC_CLOCKTYPE_HCLK ʱ��,216hz,��ӦAHB1��AHB2��AHB3��AHB4����
     *  ����RCC_CLOCKTYPE_PCLK1ʱ��,54Mhz,��ӦAPB1����
     *  ����RCC_CLOCKTYPE_PCLK2ʱ��,108Mhz,��ӦAPB2����
     */

    rcc_clk_init_handle.ClockType = (RCC_CLOCKTYPE_SYSCLK \
                                    | RCC_CLOCKTYPE_HCLK \
                                    | RCC_CLOCKTYPE_PCLK1 \
                                    | RCC_CLOCKTYPE_PCLK2);

    rcc_clk_init_handle.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;            /* ����ϵͳʱ��ʱ��ԴΪPLL */
    rcc_clk_init_handle.AHBCLKDivider = RCC_SYSCLK_DIV1;                   /* AHB��Ƶϵ��Ϊ1 */
    rcc_clk_init_handle.APB1CLKDivider = RCC_HCLK_DIV4;                    /* APB1��Ƶϵ��Ϊ4 */
    rcc_clk_init_handle.APB2CLKDivider = RCC_HCLK_DIV2;                    /* APB2��Ƶϵ��Ϊ2 */
    ret = HAL_RCC_ClockConfig(&rcc_clk_init_handle, FLASH_LATENCY_7);      /* ͬʱ����FLASH��ʱ����Ϊ7WS */
    if(ret != HAL_OK)
    {
        return 1;                                                          /* ʱ�ӳ�ʼ��ʧ�� */
    }
    return 0;
}


#ifdef  USE_FULL_ASSERT

/**
 * @brief       ��������ʾ�����ʱ��˺����������������ļ���������
 * @param       file��ָ��Դ�ļ�
 *              line��ָ�����ļ��е�����
 * @retval      ��
 */
void assert_failed(uint8_t* file, uint32_t line)
{ 
    while (1)
    {
    }
}
#endif

