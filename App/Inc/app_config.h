/**
 * app_config.h - all tunable constants for the foot sensor board firmware.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---------------- Identity ---------------- */
#define FW_MODEL_NUMBER         9000u   /* reported by PING and at address 0 */
#define FW_VERSION              1u

/* ---------------- Defaults (used when flash holds no valid config) ------- */
#define DEFAULT_DXL_ID          30u     /* motors use 1-6, 11-16, 21, 22 */
#define DEFAULT_BAUD_INDEX      7u      /* 7 = 4.5 Mbps (Dynamixel index) */
#define DEFAULT_RETURN_DELAY    0u      /* units of 2 us, 0 = reply ASAP */
#define DEFAULT_STATUS_RETURN   2u      /* 2 = reply to everything */

/* ---------------- FSR / ADC ---------------- */
#define FSR_COUNT               4u
#define ADC_STALL_TIMEOUT_MS    100u    /* no ADC sweep this long -> fault flag */

/* ---------------- RS485 / Dynamixel bus ---------------- */
#define RS485_RX_BUF_SIZE       1024u   /* circular DMA buffer, bytes */
#define RS485_TX_BUF_SIZE       300u
#define RS485_RX_TIMEOUT_BITS   25u     /* idle bit-times that end a burst */

/* Broadcast PING (ID 0xFE) replies are disabled by default: Protocol 2.0
 * does not define a collision-free slot for third-party devices, and a reply
 * could collide with the motors. Ping the board by its own ID instead. */
#define DXL_ENABLE_BROADCAST_PING 0

/* ---------------- Config storage in flash ---------------- */
/* Last 2 KB page of the 128 KB STM32G431CB flash. The linker script must
 * be shrunk to 126K so code never lands here (see integration notes). */
#define CFG_FLASH_PAGE          63u
#define CFG_FLASH_ADDR          0x0801F800u

/* ---------------- Debug CLI (USART2) ---------------- */
#define CLI_LINE_MAX            48u
#define CLI_STREAM_PERIOD_MS    50u     /* 'stream' prints at 20 Hz */

#endif /* APP_CONFIG_H */
