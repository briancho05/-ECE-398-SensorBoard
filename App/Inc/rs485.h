/**
 * rs485.h - USART1 half-duplex RS485 driver (LL), hardware DE on PA12.
 *
 *  RX: circular DMA (DMA1 CH1) + receiver-timeout and DMA half/full
 *      interrupts drain bytes into the Dynamixel slave parser.
 *  TX: DMA (DMA1 CH2); the USART drives DE itself, TC interrupt marks the
 *      end of the reply so the bus is released.
 */
#ifndef RS485_H
#define RS485_H

#include <stdint.h>
#include <stddef.h>

void     rs485_init(uint8_t baud_idx);

/** Dynamixel baud index (0..7) -> bits/s, 0 if invalid. */
uint32_t rs485_baud_from_index(uint8_t idx);

/** Reply buffer; fill it, then call rs485_start_tx(). ISR context only. */
uint8_t *rs485_tx_buffer(size_t *max_len);
void     rs485_start_tx(size_t len, uint32_t delay_us);

/** Change baud: applied immediately if idle, otherwise after the reply. */
void     rs485_set_baud(uint8_t idx);

/* Call these from the CubeMX-generated IRQ handlers (stm32g4xx_it.c). */
void     rs485_usart_irq(void);    /* USART1_IRQHandler */
void     rs485_dma_rx_irq(void);   /* DMA1_Channel1_IRQHandler */

#endif /* RS485_H */
