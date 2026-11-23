#include "r120_acq.h"
#include <string.h>
#include "timer.h"
#include "ad.h"
#include "adf4159.h"

DMA_HandleTypeDef hdma_adc1;
static uint8_t r120_acq_frame_recorded = 0U;

/*
 * One 32-bit DMA item contains one synchronized ADC pair:
 *   bits [15:0]  = ADC1 result (PA3 / IFI_H)
 *   bits [31:16] = ADC2 result (PA7 / IFQ_H)
 *
 * The linker script places RAM from 0x20000000. On STM32F765 the DTCM
 * region is reachable by DMA through the Cortex-M7 AHBS interface (RM0410).
 * This project does not enable the Cortex-M7 D-cache at present.
 */
static uint32_t g_r120_adc_dual_buffer[R120_ACQ_SAMPLE_COUNT]
    __attribute__((aligned(32)));

static volatile R120_ACQ_Status g_r120_acq_status = R120_ACQ_STATUS_IDLE;
static volatile uint8_t g_r120_acq_cleanup_pending = 0U;
static volatile uint32_t g_r120_acq_last_error = 0U;

/* Stop timer, abort DMA if it is still active, and stop both ADCs. */
static HAL_StatusTypeDef R120_ACQ_StopHardware(void)
{
    HAL_StatusTypeDef result = HAL_OK;

    /* Stop the trigger source first, so no more ADC conversions can start. */
    (void)HAL_TIM_Base_Stop(&htim2);

    /* In Normal mode DMA is already READY after a successful full transfer. */
    if (hdma_adc1.State == HAL_DMA_STATE_BUSY)
    {
        if (HAL_DMA_Abort(&hdma_adc1) != HAL_OK)
        {
            result = HAL_ERROR;
        }
    }

    /* MultiModeStart_DMA enables the master ADC overrun interrupt. */
    __HAL_ADC_DISABLE_IT(&hadc1, ADC_IT_OVR);

    /* Disable further multi-ADC DMA requests, retaining the selected DMA mode. */
    CLEAR_BIT(ADC->CCR, ADC_CCR_DDS);

    /* The multimode stop helper calls HAL_DMA_Abort unconditionally. After a
       Normal-mode transfer has completed, DMA is already READY, so stop the
       ADCs directly instead of calling HAL_ADCEx_MultiModeStop_DMA here. */
    if (HAL_ADC_Stop(&hadc1) != HAL_OK)
    {
        result = HAL_ERROR;
    }

    /* ADC2 is the slave and must also be disabled explicitly. */
    if (HAL_ADC_Stop(&hadc2) != HAL_OK)
    {
        result = HAL_ERROR;
    }

    return result;
}

HAL_StatusTypeDef R120_ACQ_Start(void)
{
    HAL_StatusTypeDef hal_status;

    /* A completed acquisition must be explicitly reset before re-use. */
    if (g_r120_acq_status != R120_ACQ_STATUS_IDLE)
    {
        return HAL_BUSY;
    }

    g_r120_acq_cleanup_pending = 0U;
    g_r120_acq_last_error = 0U;

    /* Clear old data and align the timer to the beginning of a new frame. */
    memset(g_r120_adc_dual_buffer, 0, sizeof(g_r120_adc_dual_buffer));
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, (uint32_t)TIM_FLAG_UPDATE);

    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR | ADC_FLAG_STRT);
    __HAL_ADC_CLEAR_FLAG(&hadc2, ADC_FLAG_EOC | ADC_FLAG_OVR | ADC_FLAG_STRT);

    /* In dual regular-simultaneous mode, enable the slave ADC2 first.
       HAL_ADC_Start(&hadc2) enables ADC2 but does not software-trigger it
       while ADC2 is configured as a multimode slave. */
    hal_status = HAL_ADC_Start(&hadc2);
    if (hal_status != HAL_OK)
    {
        g_r120_acq_last_error = hadc2.ErrorCode;
        g_r120_acq_status = R120_ACQ_STATUS_ERROR;
        g_r120_acq_cleanup_pending = 1U;
        return HAL_ERROR;
    }

    /* Start the master ADC1 and arm DMA for exactly 2400 packed 32-bit pairs. */
    hal_status = HAL_ADCEx_MultiModeStart_DMA(
        &hadc1,
        g_r120_adc_dual_buffer,
        R120_ACQ_SAMPLE_COUNT
    );

    /* This HAL version does not propagate HAL_DMA_Start_IT's return value
       from MultiModeStart_DMA, so also verify that the DMA handle is BUSY. */
    if ((hal_status != HAL_OK) || (hdma_adc1.State != HAL_DMA_STATE_BUSY))
    {
        g_r120_acq_last_error = hadc1.ErrorCode | hdma_adc1.ErrorCode;
        g_r120_acq_status = R120_ACQ_STATUS_ERROR;
        g_r120_acq_cleanup_pending = 1U;
        return HAL_ERROR;
    }

    /* Both ADCs and DMA are armed before TIM2 starts generating TRGO. */
    g_r120_acq_status = R120_ACQ_STATUS_RUNNING;

    if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
    {
        g_r120_acq_last_error = 0x80000001UL; /* Internal: TIM2 start failed. */
        g_r120_acq_status = R120_ACQ_STATUS_ERROR;
        g_r120_acq_cleanup_pending = 1U;
        return HAL_ERROR;
    }

    return HAL_OK;
}

void R120_ACQ_Service(void)
{
    R120_ACQ_Status status;

    status = g_r120_acq_status;

    if (((status != R120_ACQ_STATUS_DMA_DONE) &&
         (status != R120_ACQ_STATUS_ERROR)) ||
        (g_r120_acq_cleanup_pending == 0U))
    {
        return;
    }

    /* Only the main thread performs the longer HAL stop/abort operations. */
    g_r120_acq_cleanup_pending = 0U;

    if (R120_ACQ_StopHardware() != HAL_OK)
    {
        g_r120_acq_last_error |= 0x80000002UL; /* Internal: stop/abort failed. */
        g_r120_acq_status = R120_ACQ_STATUS_ERROR;
        return;
    }

    if (status == R120_ACQ_STATUS_DMA_DONE)
    {
        /* Ensure CPU observes all DMA writes before the buffer is consumed. */
        __DMB();
        g_r120_acq_status = R120_ACQ_STATUS_DONE;
    }
    /* On error, retain ERROR until the MCU is reset. */
}

R120_ACQ_Status R120_ACQ_GetStatus(void)
{
    return g_r120_acq_status;
}

const uint32_t *R120_ACQ_GetRawBuffer(void)
{
    return g_r120_adc_dual_buffer;
}

HAL_StatusTypeDef R120_ACQ_Reset(void)
{
    if (g_r120_acq_status == R120_ACQ_STATUS_RUNNING)
    {
        return HAL_BUSY;
    }

    if (g_r120_acq_status == R120_ACQ_STATUS_DMA_DONE)
    {
        R120_ACQ_Service();
    }

    /* Keep this first version conservative: after a hardware/DMA error,
       reset the MCU before attempting another acquisition. */
    if (g_r120_acq_status == R120_ACQ_STATUS_ERROR)
    {
        return HAL_ERROR;
    }

    if (g_r120_acq_status != R120_ACQ_STATUS_DONE &&
        g_r120_acq_status != R120_ACQ_STATUS_IDLE)
    {
        return HAL_ERROR;
    }

    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR | ADC_FLAG_STRT);
    __HAL_ADC_CLEAR_FLAG(&hadc2, ADC_FLAG_EOC | ADC_FLAG_OVR | ADC_FLAG_STRT);
    memset(g_r120_adc_dual_buffer, 0, sizeof(g_r120_adc_dual_buffer));

    g_r120_acq_last_error = 0U;
    g_r120_acq_cleanup_pending = 0U;
    g_r120_acq_status = R120_ACQ_STATUS_IDLE;

    return HAL_OK;
}

/* Called by the HAL multimode DMA completion path (DMA2 Stream0 IRQ). */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc != NULL) &&
        (hadc->Instance == ADC1) &&
        (g_r120_acq_status == R120_ACQ_STATUS_RUNNING))
    {
        /* Stop TIM2 immediately in the ISR to prevent a 2401st trigger.
           The slower ADC/DMA shutdown is deferred to R120_ACQ_Service(). */
        __HAL_TIM_DISABLE(&htim2);
        g_r120_acq_status = R120_ACQ_STATUS_DMA_DONE;
        g_r120_acq_cleanup_pending = 1U;
    }
}

/* Called for ADC overrun or multimode DMA transfer errors. */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc != NULL) &&
        ((hadc->Instance == ADC1) || (hadc->Instance == ADC2)) &&
        (g_r120_acq_status == R120_ACQ_STATUS_RUNNING))
    {
        g_r120_acq_last_error = hadc->ErrorCode;
        __HAL_TIM_DISABLE(&htim2);
        g_r120_acq_status = R120_ACQ_STATUS_ERROR;
        g_r120_acq_cleanup_pending = 1U;
    }
}

void measure(void)
{
	
	  /* ���ж��ⲿ����ADC/DMA��������¼� */
	  R120_ACQ_Service();

	  R120_ACQ_Status acq_status = R120_ACQ_GetStatus();
	  r120_acq_state_watch = (uint32_t)acq_status;

	  /* ��¼һ������֡��Ϣ��ֻ��¼һ�Σ� */
	  if ((acq_status == R120_ACQ_STATUS_DONE) &&
	      (r120_acq_frame_recorded == 0U))
	  {
	      const uint32_t *raw = R120_ACQ_GetRawBuffer();

	      uint32_t first = raw[0];
        //ȡ����һ��32bit�������I,Q��������12bit��
	      r120_acq_first_i = (uint16_t)(first & 0x0FFFU);
	      r120_acq_first_q = (uint16_t)((first >> 16) & 0x0FFFU);
        //���һ��������
	      r120_acq_last_raw =raw[R120_ACQ_SAMPLE_COUNT - 1U];

	      r120_acq_frame_count++;    //֡����+1
	      r120_acq_frame_recorded = 1U; //��Ǳ�֡�Ѿ���¼
	  }

	  /* �����յ��ɼ�����ʱ��������һ�β���*/
	  if (r120_acq_request == 1U)
	  {
	      r120_acq_request = 0U;   //����ɼ���ɱ�־

	      /* ������Ѿ���ɵľ�֡����Ҫ�ȸ�λ */
	      if (acq_status == R120_ACQ_STATUS_DONE)
	      {
	          r120_acq_start_return =
	              (uint32_t)R120_ACQ_Reset();

	          acq_status = R120_ACQ_GetStatus();
	      }
        //״̬����ʱ���ſ��Կ�����һ�ֲɼ�
	      if (acq_status == R120_ACQ_STATUS_IDLE)
	      {
	          r120_acq_frame_recorded = 0U;  //��ձ�֡�Ѿ���¼��ǣ�׼��������֡

	          r120_acq_start_return =(uint32_t)R120_ACQ_Start();   //����ADC�ɼ�
	      }
	      else
	      {
	          r120_acq_start_return = (uint32_t)HAL_BUSY;   //�ǿ���״̬������BUSY
	      }
	  }

	  HAL_Delay(1);
}
