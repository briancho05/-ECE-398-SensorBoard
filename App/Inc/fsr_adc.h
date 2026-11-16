/**
 * fsr_adc.h - 4 FSR channels on ADC1 (PA0, PA1, PB1, PB11).
 *
 * TIM6 TRGO (1 kHz) triggers one scan of 4 channels, each 16x hardware
 * oversampled and shifted back to 12 bits. DMA (circular) moves the 4
 * results; the transfer-complete callback copies them into the control
 * table and bumps the sample counter.
 */
#ifndef FSR_ADC_H
#define FSR_ADC_H

#include <stdint.h>
#include <stdbool.h>

bool     fsr_adc_start(void);          /* false if ADC/DMA/TIM6 failed to start */
uint32_t fsr_adc_last_sample_ms(void);

#endif /* FSR_ADC_H */
