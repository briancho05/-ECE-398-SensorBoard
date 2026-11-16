/**
 * control_table.h - Dynamixel-style register map of the foot sensor board.
 *
 *  Addr  Size  Access  Name
 *  ----  ----  ------  ---------------------------------------------
 *    0    2     R      Model Number            (FW_MODEL_NUMBER)
 *    2    4     R      Model Information       (0)
 *    6    1     R      Firmware Version
 *    7    1     RW*    ID                      0..252        (saved to flash)
 *    8    1     RW*    Baud Rate index         0..7          (saved to flash)
 *    9    1     RW*    Return Delay Time       0..254, x2 us (saved to flash)
 *   65    1     RW     LED                     0 = status pattern, 1 = force on
 *   68    1     RW     Status Return Level     0 ping only, 1 +read, 2 all
 *   70    1     R      Hardware Status flags   see HW_STATUS_*
 *  128    2     R      FSR0 raw (12-bit)
 *  130    2     R      FSR1 raw
 *  132    2     R      FSR2 raw
 *  134    2     R      FSR3 raw
 *  136    4     R      Sample counter          +1 per ADC sweep (1 kHz)
 *  140    2     R      VREFINT raw             reserved (0) - option
 *  142    2     R      Temperature raw         reserved (0) - option
 *  144    2     R      Packets handled         addressed to this board
 *  146    2     R      CRC errors              addressed to this board
 *
 *  All multi-byte values are little-endian, as on Dynamixel motors.
 *  Typical poll: READ addr 128, length 12 -> 4 FSRs + sample counter.
 */
#ifndef CONTROL_TABLE_H
#define CONTROL_TABLE_H

#include <stdint.h>
#include <stdbool.h>

#define CT_ADDR_MODEL_NUMBER    0u
#define CT_ADDR_MODEL_INFO      2u
#define CT_ADDR_FW_VERSION      6u
#define CT_ADDR_ID              7u
#define CT_ADDR_BAUD            8u
#define CT_ADDR_RETURN_DELAY    9u
#define CT_ADDR_LED             65u
#define CT_ADDR_STATUS_RETURN   68u
#define CT_ADDR_HW_STATUS       70u
#define CT_ADDR_FSR0            128u
#define CT_ADDR_SAMPLE_COUNT    136u
#define CT_ADDR_VREFINT         140u
#define CT_ADDR_TEMPERATURE     142u
#define CT_ADDR_RX_PACKETS      144u
#define CT_ADDR_CRC_ERRORS      146u
#define CT_SIZE                 148u

/* Hardware status bits (address 70) */
#define HW_STATUS_DEFAULT_CONFIG  (1u << 0)  /* flash had no valid config */
#define HW_STATUS_ADC_STALLED     (1u << 1)  /* no ADC sweep recently */
#define HW_STATUS_FLASH_ERROR     (1u << 2)  /* last config save failed */
#define HW_STATUS_ADC_START_FAIL  (1u << 3)  /* ADC/DMA/TIM6 failed to start */

typedef struct {
    bool    config_changed;     /* ID / baud / return delay written -> save */
    bool    baud_changed;       /* apply new baud after the reply is sent */
    uint8_t new_baud;
} ct_write_effects_t;

void     ct_init(uint8_t id, uint8_t baud_idx, uint8_t return_delay);

/* Bus access (called from the RS485 ISR). Return a DXL_ERR_* code. */
uint8_t  ct_read(uint16_t addr, uint16_t len, uint8_t *out);
uint8_t  ct_write(uint16_t addr, const uint8_t *data, uint16_t len, ct_write_effects_t *fx);

/* Internal access. Multi-byte accessors are interrupt-safe. */
uint8_t  ct_get_u8(uint16_t addr);
uint16_t ct_get_u16(uint16_t addr);
uint32_t ct_get_u32(uint16_t addr);
void     ct_set_u8(uint16_t addr, uint8_t v);
void     ct_inc_u16(uint16_t addr);
void     ct_set_hw_flag(uint8_t mask, bool on);

/* Called by the ADC DMA callback with one complete sweep. */
void     ct_update_fsr(const volatile uint16_t *raw);

/* Atomic snapshot of FSR values and sample counter (for the CLI). */
void     ct_snapshot_fsr(uint16_t *raw4, uint32_t *count);

#endif /* CONTROL_TABLE_H */
