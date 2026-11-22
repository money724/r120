
#ifndef R120_ACQ_H
#define R120_ACQ_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* Sampling configuration */
#define R120_ACQ_SAMPLE_RATE_HZ    400000U
#define R120_ACQ_SAMPLE_COUNT      2400U

/* Acquisition state */
typedef enum
{
    R120_ACQ_STATUS_IDLE = 0,
    R120_ACQ_STATUS_RUNNING,
    R120_ACQ_STATUS_DMA_DONE,
    R120_ACQ_STATUS_DONE,
    R120_ACQ_STATUS_ERROR

} R120_ACQ_Status;

/* Start one finite I/Q acquisition */
HAL_StatusTypeDef R120_ACQ_Start(void);

/* Process acquisition completion outside interrupt context */
void R120_ACQ_Service(void);

/* Get current acquisition status */
R120_ACQ_Status R120_ACQ_GetStatus(void);

/* Get raw packed ADC DMA buffer */
const uint32_t *R120_ACQ_GetRawBuffer(void);

/* Reset acquisition status */
HAL_StatusTypeDef R120_ACQ_Reset(void);

void measure(void);

#endif
