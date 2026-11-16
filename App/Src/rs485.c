/**
 * rs485.c - USART1 + DMA1 CH1 (RX, circular) + DMA1 CH2 (TX), LL drivers.
 *
 * CubeMX (MX_USART1_UART_Init / MX_DMA_Init) already configured:
 *   4.5 Mbps 8N1, oversampling 16, hardware DE on PA12 (active high,
 *   assertion/deassertion 8 sample times), DMA request routing, channel
 *   direction/mode/width, and NVIC priorities. This file adds buffer
 *   addresses, receiver timeout, interrupts and the runtime baud setting.
 */
#include "rs485.h"
#include "dxl_slave.h"
#include "app_config.h"
#include "platform.h"

#include "main.h"
#include "stm32g4xx_ll_usart.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_rcc.h"

#define RS485_USART       USART1
#define RS485_DMA         DMA1
#define RS485_DMA_RX_CH   LL_DMA_CHANNEL_1
#define RS485_DMA_TX_CH   LL_DMA_CHANNEL_2

static uint8_t rx_buf[RS485_RX_BUF_SIZE];
static uint8_t tx_buf[RS485_TX_BUF_SIZE];
static volatile uint16_t rx_tail;
static volatile bool     tx_busy;
static volatile bool     baud_pending;
static volatile uint8_t  baud_pending_idx;

static const uint32_t baud_table[8] = {
    9600u, 57600u, 115200u, 1000000u, 2000000u, 3000000u, 4000000u, 4500000u
};

uint32_t rs485_baud_from_index(uint8_t idx)
{
    return (idx < 8u) ? baud_table[idx] : 0u;
}

static void usart_enable_and_wait(void)
{
    LL_USART_Enable(RS485_USART);
    while (!LL_USART_IsActiveFlag_TEACK(RS485_USART) ||
           !LL_USART_IsActiveFlag_REACK(RS485_USART)) {
    }
}

/* Must only be called while no transmission is in progress. */
static void apply_baud(uint8_t idx)
{
    uint32_t baud = rs485_baud_from_index(idx);
    if (baud == 0u) {
        return;
    }
    LL_USART_Disable(RS485_USART);
    LL_USART_SetBaudRate(RS485_USART,
                         LL_RCC_GetUSARTClockFreq(LL_RCC_USART1_CLKSOURCE),
                         LL_USART_PRESCALER_DIV1, LL_USART_OVERSAMPLING_16, baud);
    usart_enable_and_wait();
}

void rs485_init(uint8_t baud_idx)
{
    rx_tail      = 0u;
    tx_busy      = false;
    baud_pending = false;

    /* --- USART: settings that need UE = 0 --- */
    LL_USART_Disable(RS485_USART);
    LL_USART_DisableOverrunDetect(RS485_USART);  /* never latch ORE; DMA keeps up */
    LL_USART_SetRxTimeout(RS485_USART, RS485_RX_TIMEOUT_BITS);
    LL_USART_EnableRxTimeout(RS485_USART);
    LL_USART_SetBaudRate(RS485_USART,
                         LL_RCC_GetUSARTClockFreq(LL_RCC_USART1_CLKSOURCE),
                         LL_USART_PRESCALER_DIV1, LL_USART_OVERSAMPLING_16,
                         rs485_baud_from_index(baud_idx) ? rs485_baud_from_index(baud_idx)
                                                         : rs485_baud_from_index(DEFAULT_BAUD_INDEX));
    usart_enable_and_wait();

    /* --- RX DMA: circular, runs forever --- */
    LL_DMA_DisableChannel(RS485_DMA, RS485_DMA_RX_CH);
    LL_DMA_SetPeriphAddress(RS485_DMA, RS485_DMA_RX_CH,
                            LL_USART_DMA_GetRegAddr(RS485_USART, LL_USART_DMA_REG_DATA_RECEIVE));
    LL_DMA_SetMemoryAddress(RS485_DMA, RS485_DMA_RX_CH, (uint32_t)rx_buf);
    LL_DMA_SetDataLength(RS485_DMA, RS485_DMA_RX_CH, RS485_RX_BUF_SIZE);
    LL_DMA_ClearFlag_GI1(RS485_DMA);
    LL_DMA_EnableIT_HT(RS485_DMA, RS485_DMA_RX_CH);   /* drain during long bursts */
    LL_DMA_EnableIT_TC(RS485_DMA, RS485_DMA_RX_CH);
    LL_DMA_EnableChannel(RS485_DMA, RS485_DMA_RX_CH);
    LL_USART_EnableDMAReq_RX(RS485_USART);

    /* --- TX DMA: peripheral address is fixed --- */
    LL_DMA_DisableChannel(RS485_DMA, RS485_DMA_TX_CH);
    LL_DMA_SetPeriphAddress(RS485_DMA, RS485_DMA_TX_CH,
                            LL_USART_DMA_GetRegAddr(RS485_USART, LL_USART_DMA_REG_DATA_TRANSMIT));

    /* --- Interrupts: receiver timeout and line errors --- */
    LL_USART_ClearFlag_RTO(RS485_USART);
    LL_USART_ClearFlag_FE(RS485_USART);
    LL_USART_ClearFlag_NE(RS485_USART);
    LL_USART_EnableIT_RTO(RS485_USART);
    LL_USART_EnableIT_ERROR(RS485_USART);
}

/* Feed every byte DMA has written since the last call into the parser. */
static void drain_rx(void)
{
    uint32_t remaining = LL_DMA_GetDataLength(RS485_DMA, RS485_DMA_RX_CH);
    uint16_t head = (uint16_t)((RS485_RX_BUF_SIZE - remaining) % RS485_RX_BUF_SIZE);

    while (rx_tail != head) {
        dxl_slave_rx_byte(rx_buf[rx_tail]);
        rx_tail = (uint16_t)((rx_tail + 1u) % RS485_RX_BUF_SIZE);
    }
}

uint8_t *rs485_tx_buffer(size_t *max_len)
{
    *max_len = RS485_TX_BUF_SIZE;
    return tx_buf;
}

void rs485_start_tx(size_t len, uint32_t delay_us)
{
    if (len == 0u || len > RS485_TX_BUF_SIZE || tx_busy) {
        return;
    }
    if (delay_us > 0u) {
        plat_delay_us(delay_us);        /* Return Delay Time (default 0) */
    }
    tx_busy = true;

    LL_DMA_DisableChannel(RS485_DMA, RS485_DMA_TX_CH);
    LL_DMA_SetMemoryAddress(RS485_DMA, RS485_DMA_TX_CH, (uint32_t)tx_buf);
    LL_DMA_SetDataLength(RS485_DMA, RS485_DMA_TX_CH, (uint32_t)len);
    LL_DMA_ClearFlag_GI2(RS485_DMA);

    LL_USART_ClearFlag_TC(RS485_USART);
    LL_USART_EnableIT_TC(RS485_USART);
    LL_USART_EnableDMAReq_TX(RS485_USART);
    LL_DMA_EnableChannel(RS485_DMA, RS485_DMA_TX_CH);  /* DE rises automatically */
}

void rs485_set_baud(uint8_t idx)
{
    CRIT_ENTER();
    if (tx_busy) {
        baud_pending_idx = idx;
        baud_pending     = true;
    } else {
        apply_baud(idx);
    }
    CRIT_EXIT();
}

void rs485_usart_irq(void)
{
    uint32_t isr = RS485_USART->ISR;

    /* Line errors: just clear them, the CRC check rejects damaged packets. */
    if (isr & (USART_ISR_FE | USART_ISR_NE | USART_ISR_PE | USART_ISR_ORE)) {
        RS485_USART->ICR = USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF | USART_ICR_ORECF;
    }

    /* Reply finished: last stop bit is out, DE has dropped. */
    if ((isr & USART_ISR_TC) && LL_USART_IsEnabledIT_TC(RS485_USART)) {
        LL_USART_DisableIT_TC(RS485_USART);
        LL_USART_DisableDMAReq_TX(RS485_USART);
        LL_DMA_DisableChannel(RS485_DMA, RS485_DMA_TX_CH);
        LL_USART_ClearFlag_TC(RS485_USART);
        tx_busy = false;

        if (baud_pending) {
            baud_pending = false;
            apply_baud(baud_pending_idx);
        }
        dxl_slave_tx_done();
    }

    /* Bus went idle: process whatever arrived. */
    if (isr & USART_ISR_RTOF) {
        LL_USART_ClearFlag_RTO(RS485_USART);
        drain_rx();
        dxl_slave_rx_idle();
    }
}

void rs485_dma_rx_irq(void)
{
    if (LL_DMA_IsActiveFlag_HT1(RS485_DMA)) {
        LL_DMA_ClearFlag_HT1(RS485_DMA);
    }
    if (LL_DMA_IsActiveFlag_TC1(RS485_DMA)) {
        LL_DMA_ClearFlag_TC1(RS485_DMA);
    }
    drain_rx();
}
