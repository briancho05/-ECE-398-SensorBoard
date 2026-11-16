/**
 * platform.c - timing helpers for the STM32 build.
 */
#include "platform.h"
#include "main.h"

uint32_t plat_millis(void)
{
    return HAL_GetTick();
}

void plat_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void plat_delay_us(uint32_t us)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000u);
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}
