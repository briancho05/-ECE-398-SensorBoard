/**
 * cli.c - line-based debug console on USART2 (HAL, interrupt RX).
 */
#include "cli.h"
#include "control_table.h"
#include "dxl_slave.h"
#include "rs485.h"
#include "app_config.h"
#include "platform.h"

#include "usart.h"     /* huart2 (CubeMX) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_RING_SIZE 64u

static uint8_t           rx_byte;
static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head;
static uint16_t          rx_tail;

static char     line[CLI_LINE_MAX];
static uint16_t line_len;
static bool     streaming;
static uint32_t last_stream_ms;

/* printf -> USART2. CubeMX's syscalls.c routes _write() to __io_putchar(). */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart2, &c, 1u, 10u);
    return ch;
}

static void arm_rx(void)
{
    (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1u);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        uint16_t next = (uint16_t)((rx_head + 1u) % RX_RING_SIZE);
        if (next != rx_tail) {          /* drop the byte if the ring is full */
            rx_ring[rx_head] = rx_byte;
            rx_head = next;
        }
        arm_rx();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        arm_rx();
    }
}

static bool getc_nb(char *c)
{
    if (rx_tail == rx_head) {
        return false;
    }
    *c = (char)rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) % RX_RING_SIZE);
    return true;
}

static void prompt(void)
{
    printf("> ");
    fflush(stdout);
}

static void print_fsr(void)
{
    uint16_t v[FSR_COUNT];
    uint32_t cnt;
    ct_snapshot_fsr(v, &cnt);
    printf("FSR0=%4u FSR1=%4u FSR2=%4u FSR3=%4u  n=%lu\r\n",
           v[0], v[1], v[2], v[3], (unsigned long)cnt);
}

static void print_info(void)
{
    uint8_t b = ct_get_u8(CT_ADDR_BAUD);
    uint8_t hw = ct_get_u8(CT_ADDR_HW_STATUS);
    printf("Foot sensor board  model %u  fw %u\r\n", (unsigned)FW_MODEL_NUMBER, (unsigned)FW_VERSION);
    printf("  ID            : %u\r\n", ct_get_u8(CT_ADDR_ID));
    printf("  baud          : %u (%lu bps)\r\n", b, (unsigned long)rs485_baud_from_index(b));
    printf("  return delay  : %u us\r\n", 2u * ct_get_u8(CT_ADDR_RETURN_DELAY));
    printf("  packets to me : %u\r\n", ct_get_u16(CT_ADDR_RX_PACKETS));
    printf("  CRC errors    : %u\r\n", ct_get_u16(CT_ADDR_CRC_ERRORS));
    printf("  status flags  : 0x%02X%s%s%s%s\r\n", hw,
           (hw & HW_STATUS_DEFAULT_CONFIG) ? " DEFAULT_CONFIG" : "",
           (hw & HW_STATUS_ADC_STALLED)    ? " ADC_STALLED" : "",
           (hw & HW_STATUS_FLASH_ERROR)    ? " FLASH_ERROR" : "",
           (hw & HW_STATUS_ADC_START_FAIL) ? " ADC_START_FAIL" : "");
}

static bool parse_uint(const char *s, unsigned *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > 255u) {
        return false;
    }
    *out = (unsigned)v;
    return true;
}

static void exec_line(char *s)
{
    char *cmd = strtok(s, " \t");
    char *arg = strtok(NULL, " \t");
    unsigned v;

    if (cmd == NULL) {
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        printf("help | info | read | stream | id [0-252] | baud [0-7] | reboot\r\n");
        printf("baud index: 0=9600 1=57600 2=115200 3=1M 4=2M 5=3M 6=4M 7=4.5M\r\n");
    } else if (strcmp(cmd, "info") == 0) {
        print_info();
    } else if (strcmp(cmd, "read") == 0) {
        print_fsr();
    } else if (strcmp(cmd, "stream") == 0) {
        streaming = true;
        last_stream_ms = plat_millis();
        printf("streaming, press any key to stop\r\n");
    } else if (strcmp(cmd, "id") == 0) {
        if (arg == NULL) {
            printf("ID = %u\r\n", ct_get_u8(CT_ADDR_ID));
        } else if (parse_uint(arg, &v) && v <= 252u) {
            ct_set_u8(CT_ADDR_ID, (uint8_t)v);
            dxl_slave_mark_config_dirty();
            printf("ID set to %u (saving)\r\n", v);
        } else {
            printf("usage: id 0..252\r\n");
        }
    } else if (strcmp(cmd, "baud") == 0) {
        if (arg == NULL) {
            uint8_t b = ct_get_u8(CT_ADDR_BAUD);
            printf("baud = %u (%lu bps)\r\n", b, (unsigned long)rs485_baud_from_index(b));
        } else if (parse_uint(arg, &v) && v <= 7u) {
            ct_set_u8(CT_ADDR_BAUD, (uint8_t)v);
            rs485_set_baud((uint8_t)v);
            dxl_slave_mark_config_dirty();
            printf("baud set to %u (%lu bps, saving)\r\n", v, (unsigned long)rs485_baud_from_index((uint8_t)v));
        } else {
            printf("usage: baud 0..7\r\n");
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        printf("rebooting\r\n");
        HAL_Delay(5);
        NVIC_SystemReset();
    } else {
        printf("unknown command '%s', type help\r\n", cmd);
    }
}

void cli_init(void)
{
    rx_head = 0u;
    rx_tail = 0u;
    line_len = 0u;
    streaming = false;
    arm_rx();
}

void cli_poll(void)
{
    /* Re-arm if a HAL error left the receiver idle */
    if (huart2.RxState == HAL_UART_STATE_READY) {
        arm_rx();
    }

    char c;
    while (getc_nb(&c)) {
        if (streaming) {                /* any key stops the stream */
            streaming = false;
            printf("\r\nstopped\r\n");
            prompt();
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (line_len > 0u) {
                printf("\r\n");
                line[line_len] = '\0';
                exec_line(line);
                line_len = 0u;
                if (!streaming) {
                    prompt();
                }
            }
        } else if (c == '\b' || c == 0x7F) {
            if (line_len > 0u) {
                line_len--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (line_len < (CLI_LINE_MAX - 1u) && c >= 0x20 && c < 0x7F) {
            line[line_len++] = c;
            putchar(c);                 /* echo */
            fflush(stdout);
        }
    }

    if (streaming) {
        uint32_t now = plat_millis();
        if ((uint32_t)(now - last_stream_ms) >= CLI_STREAM_PERIOD_MS) {
            last_stream_ms = now;
            print_fsr();
        }
    }
}
