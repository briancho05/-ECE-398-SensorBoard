/**
 * dxl_slave.h - makes the board answer the Pi like a Dynamixel device.
 *
 * Supported instructions (unicast to our ID):
 *   PING, READ, WRITE, REBOOT, FACTORY_RESET
 * Broadcast (ID 0xFE): WRITE and FACTORY_RESET are applied silently.
 * SYNC/BULK READ and WRITE are ignored (the Pi reads the board with READ).
 */
#ifndef DXL_SLAVE_H
#define DXL_SLAVE_H

#include <stdint.h>
#include <stdbool.h>

void     dxl_slave_init(void);

/* Called by the RS485 driver from interrupt context. */
void     dxl_slave_rx_byte(uint8_t b);
void     dxl_slave_rx_idle(void);
void     dxl_slave_tx_done(void);

/* Polled from the main loop. */
bool     dxl_slave_take_config_dirty(void);  /* true once per change -> save */
void     dxl_slave_mark_config_dirty(void);
bool     dxl_slave_reboot_requested(void);
uint32_t dxl_slave_last_activity_ms(void);   /* last packet addressed to us */

#endif /* DXL_SLAVE_H */
