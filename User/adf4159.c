#include "adf4159.h"
#include "adf4159_config.h"
#include "r120_signal.h"
#include "r120_range.h"
#include "r120_czt.h"
#include "r120_phase.h"
#include "r120_measure.h"
#include "r120_acq.h"
#include "r120_power.h"

/* ---------------- GPIO 便捷操作 ---------------- */
#define CLK_H()   HAL_GPIO_WritePin(ADF_CLK_GPIO_Port, ADF_CLK_Pin, GPIO_PIN_SET)
#define CLK_L()   HAL_GPIO_WritePin(ADF_CLK_GPIO_Port, ADF_CLK_Pin, GPIO_PIN_RESET)
#define DAT(x)    HAL_GPIO_WritePin(ADF_DT_GPIO_Port, ADF_DT_Pin, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define LE_H()    HAL_GPIO_WritePin(ADF_LE_GPIO_Port,  ADF_LE_Pin,  GPIO_PIN_SET)
#define LE_L()    HAL_GPIO_WritePin(ADF_LE_GPIO_Port,  ADF_LE_Pin,  GPIO_PIN_RESET)

volatile uint32_t adf4159_last_word = 0U;
volatile uint32_t adf4159_write_count = 0U;
static uint8_t s_bus_ready = 0U;

ADF4159_CFG_Plan r120_plan_cw;
ADF4159_CFG_Plan r120_plan_1g;
ADF4159_CFG_Plan r120_plan_5g;

volatile uint32_t r120_plan_result_cw;
volatile uint32_t r120_plan_result_1g;
volatile uint32_t r120_plan_result_5g;

/* ADC acquisition debug variables */
/* Set to 1 from the debugger to request one capture */
volatile uint32_t r120_acq_request = 0U;
/* Current acquisition state */
volatile uint32_t r120_acq_state_watch = 0U;
/* Last HAL start/reset result */
volatile uint32_t r120_acq_start_return = 0xFFFFFFFFU;
/* Number of completed acquisition frames */
volatile uint32_t r120_acq_frame_count = 0U;
/* First I/Q sample */
volatile uint16_t r120_acq_first_i = 0U;
volatile uint16_t r120_acq_first_q = 0U;
/* Last packed sample */
volatile uint32_t r120_acq_last_raw = 0U;
/* Prevent counting the same frame repeatedly */
static uint8_t r120_acq_frame_recorded = 0U;

/* FFT self-test result */
R120_SIG_Result r120_sig_selftest_result;
/* 0xFFFFFFFF means test has not run yet */
volatile uint32_t r120_sig_selftest_status = 0xFFFFFFFFU;

/* V0.5 range calculation */
/* Range configuration based on ADF4159 ramp plan */
R120_RANGE_Config r120_range_config;
/* Calculated distance result */
R120_RANGE_Result r120_range_result;
/* 0 = success; 0xFFFFFFFF = not executed */
volatile uint32_t r120_range_config_status = 0xFFFFFFFFU;
volatile uint32_t r120_range_calc_status = 0xFFFFFFFFU;

/* V0.6 CZT self-test */
/* CZT frequency refinement result */
R120_CZT_Result r120_czt_selftest_result;
/* Distance calculated using CZT frequency */
R120_RANGE_Result r120_czt_range_result;
/* 0 = success; 0xFFFFFFFF = not executed */
volatile uint32_t r120_czt_selftest_status = 0xFFFFFFFFU;
volatile uint32_t r120_czt_range_status = 0xFFFFFFFFU;

/* V0.7: IQ phase self-test */
/* Phase estimation result */
R120_PHASE_Result r120_phase_selftest_result;
/* 0 = success, 0xFFFFFFFF = not executed */
volatile uint32_t r120_phase_selftest_status = 0xFFFFFFFFU;


R120_MEAS_Result r120_meas_selftest_result;
volatile uint32_t r120_meas_selftest_status =
    0xFFFFFFFFU;
R120_MEAS_Config r120_meas_real_config;
R120_MEAS_Result r120_meas_real_result;
volatile uint32_t r120_meas_config_status =
    0xFFFFFFFFU;

/* ---------------- 初始化 ---------------- */
void ADF4159_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    ADF_CLK_GPIO_CLK_ENABLE();
		ADF_DAT_GPIO_CLK_ENABLE();
		ADF_LE_GPIO_CLK_ENABLE();
		ADF_CE_GPIO_CLK_ENABLE();
		ADF_MUX_GPIO_CLK_ENABLE(); 	
		AN3V3_EN_GPIO_CLK_ENABLE();                              /* GPIOE 时钟使能 */
	  AN1V8_EN_GPIO_CLK_ENABLE();                              /* GPIOD 时钟使能 */
    
    GPIO_InitStruct.Pin = AN3V3_EN_Pin;                         /* PE3引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;               /* 推挽输出 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                       /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;             /* 高速 */
    HAL_GPIO_Init(AN3V3_EN_GPIO_Port, &GPIO_InitStruct);                   /* 初始化PE3引脚 */
	  GPIO_InitStruct.Pin = ADF18_EN_Pin;                         /* PD5引脚 */
	  HAL_GPIO_Init(ADF18_EN_GPIO_Port, &GPIO_InitStruct);                   /* 初始化PD5引脚 */
		
    /*ADF_CLK_EN_Pin ADF_CLK_Pin */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_InitStruct.Pin   = ADF_CLK_Pin|ADF_CLK_EN_Pin;
    HAL_GPIO_Init(ADF_CLK_GPIO_Port, &GPIO_InitStruct);
		/*ADF_LE_Pin ADF_DT_Pin */
		GPIO_InitStruct.Pin   = ADF_DT_Pin;
    HAL_GPIO_Init(ADF_DT_GPIO_Port, &GPIO_InitStruct);		
		GPIO_InitStruct.Pin   = ADF_CLK_Pin;
    HAL_GPIO_Init(ADF_CLK_GPIO_Port, &GPIO_InitStruct);
		/*ADF_CE_Pin TRX_REN_Pin */
		GPIO_InitStruct.Pin   = ADF_CE_Pin|TRX_REN_Pin;
    HAL_GPIO_Init(ADF_CE_GPIO_Port, &GPIO_InitStruct);
		 /* BB_VREF_Pin */
		GPIO_InitStruct.Pin = BB_VREF_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BB_VREF_GPIO_Port, &GPIO_InitStruct);
    /*MUXOUT_Pin */
    GPIO_InitStruct.Pin  = MUXOUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;   //???
    HAL_GPIO_Init(MUXOUT_GPIO_Port, &GPIO_InitStruct);
}


/* Slow software delay. Not a calibrated timebase: verify CLK/LE timing
 * with an oscilloscope or logic analyzer before changing the SPI speed.
 */
static void ADF4159_BusDelay(void)
{
    for (volatile uint32_t i = 0U; i < 48U; ++i)
    {
        __NOP();
    }
}

HAL_StatusTypeDef ADF4159_BusInit(void)
{
    /* MX_GPIO_Init() must already have been called. */
    HAL_GPIO_WritePin(ADF_CLK_GPIO_Port, ADF_CLK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ADF_DT_GPIO_Port, ADF_DT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ADF_LE_GPIO_Port, ADF_LE_Pin, GPIO_PIN_RESET);
    ADF4159_BusDelay();
    s_bus_ready = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef ADF4159_WriteWord(uint32_t word)
{
    if (s_bus_ready == 0U)
    {
        return HAL_ERROR;
    }

    /* ADF4159 accepts a 32-bit serial word MSB first. Data is sampled
     * on CLK rising edges and latched into the addressed register on LE rising.
     */
    HAL_GPIO_WritePin(ADF_LE_GPIO_Port, ADF_LE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ADF_CLK_GPIO_Port, ADF_CLK_Pin, GPIO_PIN_RESET);
    ADF4159_BusDelay();

    for (uint32_t mask = 0x80000000UL; mask != 0U; mask >>= 1U)
    {
        HAL_GPIO_WritePin(ADF_DT_GPIO_Port,
                          ADF_DT_Pin,
                          ((word & mask) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        ADF4159_BusDelay();
        HAL_GPIO_WritePin(ADF_CLK_GPIO_Port, ADF_CLK_Pin, GPIO_PIN_SET);
        ADF4159_BusDelay();
        HAL_GPIO_WritePin(ADF_CLK_GPIO_Port, ADF_CLK_Pin, GPIO_PIN_RESET);
        ADF4159_BusDelay();
    }

    HAL_GPIO_WritePin(ADF_LE_GPIO_Port, ADF_LE_Pin, GPIO_PIN_SET);
    ADF4159_BusDelay();
    HAL_GPIO_WritePin(ADF_LE_GPIO_Port, ADF_LE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ADF_DT_GPIO_Port, ADF_DT_Pin, GPIO_PIN_RESET);

    adf4159_last_word = word;
    ++adf4159_write_count;
    return HAL_OK;
}

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

void ADF4159_Config(void)
{
	R120_PowerSafeOff();
	if (ADF4159_BusInit() != HAL_OK)
	{
			Error_Handler();
	}

  /* 123GHz 定频配置 */
  r120_plan_result_cw =ADF4159_CFG_PlanFixedHz(123000000000ULL,&r120_plan_cw);
  /* 122~123GHz，6ms 扫频 */
  r120_plan_result_1g =ADF4159_CFG_PlanRampHz(122000000000ULL,123000000000ULL,6000U,&r120_plan_1g);
  /* 120~125GHz，6ms 扫频 */
  r120_plan_result_5g =ADF4159_CFG_PlanRampHz(120000000000ULL,125000000000ULL,6000U,&r120_plan_5g);

  /* 上电时运行一次FFT自检
     T使用仿真I/Q采样数据 */
  r120_sig_selftest_status =(uint32_t)R120_SIG_RunSelfTest(&r120_sig_selftest_result);  //FFT自检测距

  /* V0.5: FFT结果 -> 距离 */
  /* 仅在前序两个步骤都成功时，才进行计算 */
  if ((r120_plan_result_5g == 0U) &&(r120_sig_selftest_status == (uint32_t)R120_SIG_OK))
  {
		/* 使用ADF4159算出来的斜坡参数 */
		r120_range_config_status =(uint32_t)R120_RANGE_ConfigFromPlan(&r120_plan_5g,0.0,     /* 最小测距: 0 m */
						5.1,     /* 最大测距: 5.1 m */
						0.0,     /* 校准偏移量: 0 m */
						&r120_range_config);

		if (r120_range_config_status ==(uint32_t)R120_RANGE_OK)
		{
			/* 将FFT拍频转换为距离 */
			r120_range_calc_status =(uint32_t)R120_RANGE_FromSignal(R120_SIG_OK,&r120_sig_selftest_result,&r120_range_config,&r120_range_result);
		}
  }


  /* V0.6: CZT频率精准自检*/
  /* 生成仿真数据I/Q数据，执行FFT + CZT */
  r120_czt_selftest_status =(uint32_t)R120_CZT_RunSelfTest(&r120_czt_selftest_result);      //CCZT精准测距
  /* 使用CZT精修后的拍频计算距离*/
  if ((r120_czt_selftest_status ==(uint32_t)R120_CZT_OK) && (r120_range_config_status ==(uint32_t)R120_RANGE_OK))
  {
      r120_czt_range_status =(uint32_t)R120_RANGE_FromBeatHz(r120_czt_selftest_result.refined_signed_beat_hz,&r120_range_config,&r120_czt_range_result );
  }


  /* V0.7: 相位估算自检 */
  /* 仿真IQ数据 -> FFT -> CZT -> 相位计算 */
  r120_phase_selftest_status =(uint32_t)R120_PHASE_RunSelfTest(&r120_phase_selftest_result);    //相位自检

  /* V0.8:完整算法自检 */
  r120_meas_selftest_status =(uint32_t)R120_MEAS_RunSelfTest(&r120_meas_selftest_result
      );
  /* 为后续真实ADC采样帧准备参数 */
  if (r120_range_config_status ==(uint32_t)R120_RANGE_OK)
  {
      r120_meas_config_status =(uint32_t)R120_MEAS_MakeConfig(&r120_range_config, R120_ACQ_SAMPLE_RATE_HZ,R120_ACQ_SAMPLE_COUNT, &r120_meas_real_config);  //生成正式测量的算法配置
  }
}



#if 0
/* 216MHz 下约 30~50ns，满足手册 t2/t3>=10ns, t4/t5>=25ns, t6>=10ns, t7>=20ns */
static void ADF4159_Delay(void)
{
    volatile uint32_t n = 8;
    while (n--) __NOP();
}

/* R0 镜像（MUXOUT/斜坡位需要回写 R0） */
static uint32_t s_r0 = 0;

/* ---------------- 底层：32 位写入，MSB 先发，LE 上升沿锁存 ---------------- */
void ADF4159_WriteReg(uint32_t reg)
{
    uint8_t i;
    LE_L();
    for (i = 0; i < 32; i++) 
	  {
        CLK_L();
        DAT(reg & 0x80000000u);
        ADF4159_Delay();          /* DATA 建立/保持 */
        CLK_H();                  /* 上升沿锁入移位寄存器 */
        ADF4159_Delay();
        reg <<= 1;
    }
    CLK_L();
    ADF4159_Delay();              /* CLK 到 LE 建立时间 t6 */
    LE_H();                       /* LE 上升沿：载入由 DB[2:0] 选定的锁存器 */
    ADF4159_Delay();              /* LE 脉宽 t7 */
    LE_L();
}

/* ---------------- 初始化 ---------------- */
void ADF4159_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    ADF_CLK_GPIO_CLK_ENABLE();
		ADF_DAT_GPIO_CLK_ENABLE();
		ADF_LE_GPIO_CLK_ENABLE();
		ADF_CE_GPIO_CLK_ENABLE();
		ADF_MUX_GPIO_CLK_ENABLE(); 	
		AN3V3_EN_GPIO_CLK_ENABLE();                              /* GPIOE 时钟使能 */
	  AN1V8_EN_GPIO_CLK_ENABLE();                              /* GPIOD 时钟使能 */
    
    GPIO_InitStruct.Pin = AN3V3_EN_Pin;                         /* PE3引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;               /* 推挽输出 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                       /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;             /* 高速 */
    HAL_GPIO_Init(AN3V3_EN_GPIO_Port, &GPIO_InitStruct);                   /* 初始化PE3引脚 */
	  GPIO_InitStruct.Pin = ADF18_EN_Pin;                         /* PD5引脚 */
	  HAL_GPIO_Init(ADF18_EN_GPIO_Port, &GPIO_InitStruct);                   /* 初始化PD5引脚 */
		
    /*Configure GPIO pins : ADF_CLK_EN_Pin ADF_CLK_Pin */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Pin   = ADF_CLK_Pin|ADF_CLK_EN_Pin;
    HAL_GPIO_Init(ADF_CLK_GPIO_Port, &GPIO_InitStruct);
		/*Configure GPIO pins : ADF_LE_Pin ADF_DT_Pin */
		GPIO_InitStruct.Pin   = ADF_DT_Pin;
    HAL_GPIO_Init(ADF_DT_GPIO_Port, &GPIO_InitStruct);		
		GPIO_InitStruct.Pin   = ADF_CLK_Pin;
    HAL_GPIO_Init(ADF_CLK_GPIO_Port, &GPIO_InitStruct);
		/*Configure GPIO pins : ADF_CE_Pin TRX_REN_Pin */
		GPIO_InitStruct.Pin   = ADF_CE_Pin|TRX_REN_Pin;
    HAL_GPIO_Init(ADF_CE_GPIO_Port, &GPIO_InitStruct);
		 /*Configure GPIO pin : BB_VREF_Pin */
		GPIO_InitStruct.Pin = BB_VREF_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BB_VREF_GPIO_Port, &GPIO_InitStruct);
    /*Configure GPIO pin : MUXOUT_Pin */
    GPIO_InitStruct.Pin  = MUXOUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;   //???
    HAL_GPIO_Init(MUXOUT_GPIO_Port, &GPIO_InitStruct);

    CLK_L(); LE_L(); DAT(0);
    HAL_GPIO_WritePin(ADF_CE_GPIO_Port, ADF_CE_Pin, GPIO_PIN_SET);  /* CE=1 使能 */
    HAL_Delay(5);

    /* 手册“初始化序列”：R7→R6(×2)→R5(×2)→R4(×2)→R3→R2→R1→R0 */
    ADF4159_WriteReg(0x00000007u);                                  /* R7 默认 */
    ADF4159_WriteReg((0u << 23) | (200u << 3) | 0x6u);              /* R6 STEP_SEL=0 */
    ADF4159_WriteReg((1u << 23) | (200u << 3) | 0x6u);              /* R6 STEP_SEL=1 */
    ADF4159_WriteReg((0u << 23) | (20972u << 3) | 0x5u);            /* R5 DEV_SEL=0 */
    ADF4159_WriteReg((1u << 23) | (20972u << 3) | 0x5u);            /* R5 DEV_SEL=1 */
    ADF4159_WriteReg((100u << 7) | (3u << 19) | (0u << 6) | 0x4u);  /* R4 CLK_SEL=0 */
    ADF4159_WriteReg((100u << 7) | (3u << 19) | (1u << 6) | 0x4u);  /* R4 CLK_SEL=1 */
    /* R3: DB17=1(必须), LDP=1, PD 极性, 斜坡=连续锯齿 */
    s_r0 = 0;
    ADF4159_WriteReg((1u << 17) | (1u << 7) | (ADF4159_PD_POL << 6) | 0x3u);
    ADF4159_WriteReg(((uint32_t)ADF4159_CP_CODE << 24) |
                     ((uint32_t)(ADF4159_R_DIV - 1) << 15) |
                     (100u << 3) | 0x2u);                            /* R2 */
    ADF4159_WriteReg(0x00000001u);                                  /* R1 */
    ADF4159_WriteReg((23u << 15) | 0x0u);                           /* R0, INT=23 */
}

/* ---------------- PFD 频率 ---------------- */
double ADF4159_PfdFreq(void)
{
    return ADF4159_REFIN_HZ * (1.0 + ADF4159_REF_DOU) /
           (ADF4159_R_DIV * (1.0 + ADF4159_REF_DIV2));
}

/* ---------------- 小数 N 设频：RF = (INT + FRAC/2^25) × fPFD ---------------- */
void ADF4159_SetFrequency(double freq_hz)
{
    double  fpfd = ADF4159_PfdFreq();
    uint32_t INT = (uint32_t)(freq_hz / fpfd);
    uint64_t frac25;
    uint32_t frac_msb, frac_lsb, r2;

    if (INT < 23)  INT = 23;
    if (INT > 4095) INT = 4095;
    frac25 = (uint64_t)((freq_hz / fpfd - (double)INT) * 33554432.0 + 0.5);
    if (frac25 >= 33554432ull) { frac25 = 0; INT++; }   /* 四舍五入进位 */

    frac_msb = (uint32_t)(frac25 >> 13) & 0xFFFu;       /* 12 位 MSB → R0.DB[14:3] */
    frac_lsb = (uint32_t)(frac25 & 0x1FFFu);            /* 13 位 LSB → R1.DB[27:15] */

    /* R2：>8GHz 用 8/9 预分频 (DB22)，否则 4/5 */
    r2 = ((uint32_t)ADF4159_CP_CODE << 24) |
         ((freq_hz >= 8.0e9 ? 1u : 0u) << 22) |
         ((uint32_t)ADF4159_REF_DOU << 20) |
         ((uint32_t)(ADF4159_R_DIV - 1) << 15) |
         (100u << 3) | 0x2u;
    ADF4159_WriteReg(r2);

    /* 双缓冲：先写 R1，再写 R0 才生效 */
    ADF4159_WriteReg((frac_lsb << 15) | (0u << 3) | 0x1u);   /* R1, 相位=0 */
    s_r0 = (0u << 31) | (0u << 27) | (INT << 15) | (frac_msb << 3);
    ADF4159_WriteReg(s_r0);                                   /* R0 触发更新 */
}

/* ---------------- 整数 N 模式（禁用 Σ-Δ，消除小数杂散与 fPFD/2^26 失调） */
void ADF4159_SetIntegerFrequency(double freq_hz)
{
    double  fpfd = ADF4159_PfdFreq();
    uint32_t INT = (uint32_t)(freq_hz / fpfd + 0.5);
    uint32_t r2;

    if (INT < 23)  INT = 23;
    if (INT > 4095) INT = 4095;

    r2 = ((uint32_t)ADF4159_CP_CODE << 24) |
         ((freq_hz >= 8.0e9 ? 1u : 0u) << 22) |
         ((uint32_t)ADF4159_REF_DOU << 20) |
         ((uint32_t)(ADF4159_R_DIV - 1) << 15) |
         (100u << 3) | 0x2u;
    ADF4159_WriteReg(r2);
    ADF4159_WriteReg(0x00000001u);                       /* R1: FRAC LSB=0 */

    /* R4: DB[30:26]=01110 禁用 Σ-Δ；FRAC 必须为 0 */
    ADF4159_WriteReg((0x0Eu << 26) | (100u << 7) | 0x4u);
    /* 写 R4 后写 R3 两次：分频器复位 1 → 0 */
    ADF4159_WriteReg((1u << 17) | (1u << 3) | (1u << 7) | (ADF4159_PD_POL << 6) | 0x3u);
    ADF4159_WriteReg((1u << 17) | (1u << 7) | (ADF4159_PD_POL << 6) | 0x3u);

    s_r0 = (0u << 31) | (0u << 27) | (INT << 15);
    ADF4159_WriteReg(s_r0);
}

/* ---------------- FMCW 斜坡（手册“FMCW 雷达斜坡设置成功范例”算法） ---------------- */
int ADF4159_FmcwRamp(const ADF4159_FmcwConfig *cfg)
{
    double  fpfd  = ADF4159_PfdFreq();
    double  fres  = fpfd / 33554432.0;              /* fRES = fPFD/2^25 */
    double  fdev  = cfg->bandwidth_hz / cfg->steps; /* 每步频偏 */
    uint32_t dev_off = 0, dev, clk1;
    uint32_t r3, r4a, r4b, r5a, r5b, r6a, r6b, r7;

    if (cfg->steps == 0 || cfg->steps > 1048575u) return -1;

    /* 选最小 DEV_OFFSET 使 DEV 落入 16 位（分辨率最高） */
    while (dev_off < 16) {
        dev = (uint32_t)(fdev / (fres * (double)(1ul << dev_off)) + 0.5);
        if (dev <= 65535u) break;
        dev_off++;
    }
    if (dev_off >= 16) return -2;
    dev = (uint32_t)(fdev / (fres * (double)(1ul << dev_off)) + 0.5);

    /* CLK2=1, CLK1 = 步进时间 × fPFD（手册公式 8），须 >1 且 ≤4095 */
    clk1 = (uint32_t)(cfg->step_time_s * fpfd + 0.5);
    if (clk1 < 2 || clk1 > 4095u) return -3;

    ADF4159_SetFrequency(cfg->start_freq_hz);        /* 先锁定起始频率 */

    r7  = 0x7u;                                                     /* R7 默认 */
    r6a = (0u << 23) | (cfg->steps << 3) | 0x6u;                    /* R6 STEP_SEL=0 */
    r6b = (1u << 23) | (cfg->steps << 3) | 0x6u;                    /* R6 STEP_SEL=1 */
    r5a = (0u << 23) | (dev_off << 19) | (dev << 3) | 0x5u;         /* R5 DEV_SEL=0 */
    r5b = (1u << 23) | (dev_off << 19) | (dev << 3) | 0x5u;         /* R5 DEV_SEL=1 */
    /* R4: DB[20:19]=11 斜坡分频器, CLK2=1, CLK DIV SEL 先 0 后 1 */
    r4a = (1u << 20) | (1u << 19) | (1u << 7) | (0u << 6) | 0x4u;
    r4b = (1u << 20) | (1u << 19) | (1u << 7) | (1u << 6) | 0x4u;
    /* R3: 斜坡模式 + DB17 斜坡延迟使能 + LDP + 极性 */
    r3  = (1u << 17) | ((uint32_t)cfg->mode << 10) |
          (1u << 7) | (ADF4159_PD_POL << 6) | 0x3u;

    /* 手册斜坡编程序列 */
    ADF4159_WriteReg(r7);
    ADF4159_WriteReg(r6a);  ADF4159_WriteReg(r6b);
    ADF4159_WriteReg(r5a);  ADF4159_WriteReg(r5b);
    ADF4159_WriteReg(r4a);  ADF4159_WriteReg(r4b);
    ADF4159_WriteReg(r3);
    ADF4159_WriteReg(((uint32_t)ADF4159_CP_CODE << 24) |
                     ((cfg->start_freq_hz >= 8.0e9 ? 1u : 0u) << 22) |
                     ((uint32_t)ADF4159_REF_DOU << 20) |
                     ((uint32_t)(ADF4159_R_DIV - 1) << 15) |
                     (clk1 << 3) | 0x2u);                            /* R2 含 CLK1 */
    ADF4159_WriteReg(s_r0 | (1u << 31));                             /* R0.DB31=1 启动 */
    return 0;
}

void ADF4159_RampStop(void)               /* ADF4159_RampStop();  需要停止时调用 */
{
    s_r0 &= ~(1u << 31);
    ADF4159_WriteReg(s_r0);
}

/* ---------------- MUXOUT / 锁定检测 ---------------- */
void ADF4159_SetMuxout(uint8_t mux)
{
    s_r0 = (s_r0 & ~(0xFu << 27)) | ((uint32_t)(mux & 0xF) << 27);
    ADF4159_WriteReg(s_r0);
}

uint8_t ADF4159_WaitLock(uint32_t timeout_ms)
{
    ADF4159_SetMuxout(6);            /* 0110 = 数字锁定检测 */
    HAL_Delay(1);                    /* 写 R0 后锁定检测重新开始 */
    while (timeout_ms--) {
        if (HAL_GPIO_ReadPin(MUXOUT_GPIO_Port, MUXOUT_Pin) == GPIO_PIN_SET)
            return 1;
        HAL_Delay(1);
    }
    return 0;
}

void ADF4159_PowerDown(uint8_t en)
{
    uint32_t r3 = (1u << 17) | (1u << 7) | (ADF4159_PD_POL << 6) | 0x3u;
    if (en) r3 |= (1u << 5);         /* R3.DB5 = 1 关断（寄存器内容保留） */
    ADF4159_WriteReg(r3);
}

void ADF4159_ModeConfig(void)
{
	/* 1) 固定频点：12.002 GHz */
    ADF4159_SetFrequency(12.002e9);
    if (!ADF4159_WaitLock(20))
        printf("ADF4159 未锁定!\r\n");

    /* 2) FMCW 锯齿斜坡：12.000 GHz 起，扫 50 MHz，200 步，每步 10us（总 2ms） */
    ADF4159_FmcwConfig ramp = {
        .start_freq_hz = 12.000e9,
        .bandwidth_hz  = 50e6,
        .steps         = 200,
        .step_time_s   = 10e-6,
        .mode          = ADF4159_RAMP_CONT_SAWTOOTH,          /* 三角斜坡把 mode 改为 ADF4159_RAMP_CONT_TRIANGLE 即可 */
    };
    if (ADF4159_FmcwRamp(&ramp) != 0)
        printf("斜坡参数错误（检查 steps/step_time）\r\n");
}
#endif