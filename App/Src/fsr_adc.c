/**
 * fsr_adc.c - ADC1 + DMA + TIM6 sampling of the four FSRs.
 */
#include "fsr_adc.h"
#include "control_table.h"
#include "app_config.h"
#include "platform.h"

#include "adc.h"   /* hadc1 (CubeMX) */
#include "tim.h"   /* htim6 (CubeMX) */

/* DMA writes half-words; HAL wants a uint32_t* but counts transfers. */
static volatile uint16_t adc_dma_buf[FSR_COUNT] __attribute__((aligned(4)));
static volatile uint32_t last_sample_ms;

bool fsr_adc_start(void)
{
    last_sample_ms = plat_millis();

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
        return false;
    }
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, FSR_COUNT) != HAL_OK) {
        return false;
    }
    if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {   /* TRGO starts the scans */
        return false;
    }
    return true;
}

uint32_t fsr_adc_last_sample_ms(void)
{
    return last_sample_ms;
}

/* DMA transfer complete = one full 4-channel sweep (overrides HAL weak). */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        ct_update_fsr(adc_dma_buf);
        last_sample_ms = plat_millis();
    }
}
