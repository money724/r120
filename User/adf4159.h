#ifndef __ADF4159_H
#define __ADF4159_H
#include "stm32f7xx_hal.h"

/* ---------------- 引脚配置 ---------------- */
#define ADF_CLK_EN_Pin GPIO_PIN_13
#define ADF_CLK_EN_GPIO_Port GPIOC
#define AN3V3_EN_Pin GPIO_PIN_3
#define AN3V3_EN_GPIO_Port GPIOE
#define ADF_LE_Pin GPIO_PIN_8
#define ADF_LE_GPIO_Port GPIOB
#define ADF18_EN_Pin GPIO_PIN_5
#define ADF18_EN_GPIO_Port GPIOD
#define ADF_CLK_Pin GPIO_PIN_10
#define ADF_CLK_GPIO_Port GPIOC
#define ADF_CE_Pin GPIO_PIN_12
#define ADF_CE_GPIO_Port GPIOA
#define MUXOUT_Pin GPIO_PIN_1
#define MUXOUT_GPIO_Port GPIOE
#define ADF_DT_Pin GPIO_PIN_5
#define ADF_DT_GPIO_Port GPIOB
#define BB_VREF_Pin GPIO_PIN_5
#define BB_VREF_GPIO_Port GPIOA
#define TRX_REN_Pin GPIO_PIN_2
#define TRX_REN_GPIO_Port GPIOA

#define AN1V8_EN_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)   /* PD口时钟使能 */
#define AN3V3_EN_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */
#define ADF_CLK_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)   /* PC口时钟使能 */
#define ADF_DAT_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB口时钟使能 */
#define ADF_LE_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB口时钟使能 */
#define ADF_CE_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)   /* PA口时钟使能 */
#define ADF_MUX_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

HAL_StatusTypeDef ADF4159_BusInit(void);
HAL_StatusTypeDef ADF4159_WriteWord(uint32_t word);

/* Debug-only software records. They do not prove the chip received data. */
extern  volatile uint32_t adf4159_last_word;
extern volatile uint32_t adf4159_write_count;

/* ADC acquisition debug variables */
/* Set to 1 from the debugger to request one capture */
extern volatile uint32_t r120_acq_request;
/* Current acquisition state */
extern volatile uint32_t r120_acq_state_watch;
/* Last HAL start/reset result */
extern volatile uint32_t r120_acq_start_return;
/* Number of completed acquisition frames */
extern volatile uint32_t r120_acq_frame_count;
/* First I/Q sample */
extern volatile uint16_t r120_acq_first_i;
extern volatile uint16_t r120_acq_first_q;
/* Last packed sample */
extern volatile uint32_t r120_acq_last_raw;
/* Prevent counting the same frame repeatedly */

void ADF4159_Init(void);
void ADF4159_Config(void);

#if 0
#define AN3V3_EN(x)        do{ x ? \
                              HAL_GPIO_WritePin(AN3V3_EN_GPIO_PORT, AN3V3_EN_GPIO_PIN, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(AN3V3_EN_GPIO_PORT, AN3V3_EN_GPIO_PIN, GPIO_PIN_RESET); \
                          }while(0)       /* AN3V3 */

#define AN1V8_EN(x)        do{ x ? \
															HAL_GPIO_WritePin(AN1V8_EN_GPIO_PORT, AN1V8_EN_GPIO_PIN, GPIO_PIN_SET) : \
															HAL_GPIO_WritePin(AN1V8_EN_GPIO_PORT, AN1V8_EN_GPIO_PIN, GPIO_PIN_RESET); \
													}while(0)       /* AN3V3 */

/* ---------------- 频率与电荷泵配置 ---------------- */
#define ADF4159_REFIN_HZ   100000000.0  /* 外部参考频率 REFIN */
#define ADF4159_R_DIV      1            /* R 分频比 1~32 */
#define ADF4159_REF_DOU    0            /* 参考倍频器 D: 0/1 (R2.DB20) */
#define ADF4159_REF_DIV2   0            /* 参考 2 分频 T: 0/1 (R2.DB21) */
#define ADF4159_CP_CODE    7            /* 电荷泵电流代码, 7=2.5mA (R2.DB[27:24]) */
#define ADF4159_PD_POL     1            /* 鉴相器极性: 1=正 (VCO 正调谐) */

typedef enum {
    ADF4159_RAMP_CONT_SAWTOOTH = 0,   /* 连续锯齿斜坡   R3.DB[11:10]=00 */
    ADF4159_RAMP_CONT_TRIANGLE = 1,   /* 连续三角斜坡   R3.DB[11:10]=01 */
    ADF4159_RAMP_SAW_BURST     = 2,   /* 单锯齿突发     R3.DB[11:10]=10 */
    ADF4159_RAMP_SINGLE_BURST  = 3    /* 单斜坡突发     R3.DB[11:10]=11 */
} ADF4159_RampMode;

typedef struct {
    double       start_freq_hz;  /* 斜坡起始频率 (R0 锁定频率)      */
    double       bandwidth_hz;   /* 单程扫频带宽 (三角=单程)        */
    uint32_t     steps;          /* 步进数 1~1048575                */
    double       step_time_s;    /* 每步时间 (s)，如 10e-6           */
    ADF4159_RampMode mode;
} ADF4159_FmcwConfig;

void    ADF4159_Init(void);
void    ADF4159_WriteReg(uint32_t reg);
double  ADF4159_PfdFreq(void);
void    ADF4159_SetFrequency(double freq_hz);          /* 小数 N 模式 */
void    ADF4159_SetIntegerFrequency(double freq_hz);   /* 整数 N 模式(低杂散) */
int     ADF4159_FmcwRamp(const ADF4159_FmcwConfig *cfg); /* 配置并启动斜坡, 0=成功 */
void    ADF4159_RampStop(void);
void    ADF4159_SetMuxout(uint8_t mux);   /* R0.DB[30:27], 6=数字锁定检测 */
uint8_t ADF4159_WaitLock(uint32_t timeout_ms);
void    ADF4159_PowerDown(uint8_t en);
#endif


#endif
