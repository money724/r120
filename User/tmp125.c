#include "tmp125.h"
#include "stm32f7xx_hal.h"
#include "delay.h"

/* 简单微秒延时，F7主频高，根据实际系统时钟微调 */
static void TMP125_DelayUs(uint32_t us)
{
   delay_us(us);
}

/**
 * @brief TMP125 GPIO初始化
 */
void TMP125_Init(void)
{
    GPIO_InitTypeDef gpio_conf = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* SCK PE2 Output */
    gpio_conf.Pin = TMP125_SCK_PIN;
    gpio_conf.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_conf.Pull = GPIO_NOPULL;
    gpio_conf.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TMP125_SCK_PORT, &gpio_conf);

    /* CS PE4 Output */
    gpio_conf.Pin = TMP125_CS_PIN;
    HAL_GPIO_Init(TMP125_CS_PORT, &gpio_conf);

    /* SI PE6 Output */
    gpio_conf.Pin = TMP125_SI_PIN;
    HAL_GPIO_Init(TMP125_SI_PORT, &gpio_conf);

    /* SO PE5 Input */
    gpio_conf.Pin = TMP125_SO_PIN;
    gpio_conf.Mode = GPIO_MODE_INPUT;
    gpio_conf.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(TMP125_SO_PORT, &gpio_conf);

    /* 初始状态：CS拉高，SCK低，SI低（正常工作模式） */
    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TMP125_SI_PORT, TMP125_SI_PIN, GPIO_PIN_RESET);
}

/**
 * @brief 读取16bit原始寄存器值
 * @retval 原始16位数据，高6位无效，bit14~bit5为10bit温度补码
 */
int16_t TMP125_ReadRaw(void)
{
    uint16_t raw = 0U;

    /* CS拉低，锁存上一次转换结果 */
    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_RESET);
    TMP125_DelayUs(1);

    /* 读取16个bit，下降沿输出SO，手册时序：SCK先高再产生下降沿 */
    for(int i = 0; i < 16; i++)
    {
        HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_SET);
        TMP125_DelayUs(1);
        
			  raw <<= 1U;
			  if(HAL_GPIO_ReadPin(TMP125_SO_PORT, TMP125_SO_PIN) == GPIO_PIN_SET)
        {
            raw |= 0x0001U;
        }
        /* SCK下降沿，SO输出数据位 */
        HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_RESET);
        TMP125_DelayUs(1);

//        raw <<= 1U;
//        if(HAL_GPIO_ReadPin(TMP125_SO_PORT, TMP125_SO_PIN) == GPIO_PIN_SET)
//        {
//            raw |= 0x0001U;
//        }
    }

    /* CS释放 */
    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_SET);
    TMP125_DelayUs(1);

    return (int16_t)raw;
}

/**
 * @brief 读取温度，返回摄氏度float
 * @note 有效位 D14~D5 共10位2补码，LSB=0.25℃，D15恒0，D4~D0与D5重复，丢弃低5位
 * @retval 温度，单位℃
 */
float TMP125_ReadTemperature(void)
{
    int16_t raw = TMP125_ReadRaw();
    /* 右移5位，取出10bit有效温度(T9~T0，D14~D5) */
    int16_t temp_10bit = raw >> 5;

    /* 10bit符号扩展到16bit */
    if(temp_10bit & 0x0200)
    {
        temp_10bit |= 0xFC00;
    }

    float temp_c = temp_10bit * 0.25f;
    return temp_c;
}

/**
 * @brief 进入关断休眠模式，SI在第3个SCK上升沿置高，完整16时钟
 */
void TMP125_EnterShutdown(void)
{
    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_RESET);

    for(int i = 0; i < 16; i++)
    {
        HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_SET);

        /* 第3位上升沿SI置高，i从0开始，i=2是第3个时钟 */
        if(i == 2)
        {
            HAL_GPIO_WritePin(TMP125_SI_PORT, TMP125_SI_PIN, GPIO_PIN_SET);
        }

        TMP125_DelayUs(1);
        HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_RESET);
        TMP125_DelayUs(1);
    }

    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief 退出休眠，SI拉低，执行完整16时钟
 */
void TMP125_ExitShutdown(void)
{
    HAL_GPIO_WritePin(TMP125_SI_PORT, TMP125_SI_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_RESET);

    for(int i = 0; i <16; i++)
    {
        HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_SET);
        TMP125_DelayUs(1);
        HAL_GPIO_WritePin(TMP125_SCK_PORT, TMP125_SCK_PIN, GPIO_PIN_RESET);
        TMP125_DelayUs(1);
    }

    HAL_GPIO_WritePin(TMP125_CS_PORT, TMP125_CS_PIN, GPIO_PIN_SET);
}
