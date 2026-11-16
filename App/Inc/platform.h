/**
 * platform.h - tiny hardware abstraction so the protocol logic can also be
 * compiled and tested on a PC (define UNIT_TEST for host builds).
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#ifdef UNIT_TEST

#define CRIT_ENTER()  do {
#define CRIT_EXIT()   } while (0)
uint32_t plat_millis(void);
static inline void plat_delay_us(uint32_t us) { (void)us; }
static inline void plat_init(void) {}

#else

#include "stm32g4xx.h"

/* Save/restore PRIMASK so these nest safely and work from ISRs too. */
#define CRIT_ENTER()  do { uint32_t _primask = __get_PRIMASK(); __disable_irq();
#define CRIT_EXIT()   __set_PRIMASK(_primask); } while (0)

uint32_t plat_millis(void);          /* wraps HAL_GetTick() */
void     plat_init(void);            /* enables the DWT cycle counter */
void     plat_delay_us(uint32_t us); /* busy-wait using DWT->CYCCNT */

#endif /* UNIT_TEST */

#endif /* PLATFORM_H */
