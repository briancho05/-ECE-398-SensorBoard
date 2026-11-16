/**
 * cli.h - debug console on USART2 (CH340C, USB-C), 921600 8N1.
 *
 * Commands (type, then Enter):
 *   help              list commands
 *   info              ID, baud, counters, status flags
 *   read              print the 4 FSR values once
 *   stream            print FSR values at 20 Hz until any key is pressed
 *   id [n]            show / set Dynamixel ID (saved to flash)
 *   baud [0..7]       show / set bus baud index (saved to flash)
 *   reboot            software reset
 */
#ifndef CLI_H
#define CLI_H

void cli_init(void);
void cli_poll(void);    /* call every main-loop iteration */

#endif /* CLI_H */
