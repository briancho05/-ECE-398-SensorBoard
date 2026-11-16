/**
 * app.c - start-up order and the (non time-critical) main loop.
 *
 * Time-critical work (answering the Pi, sampling the FSRs) happens in
 * interrupts and DMA. The main loop only handles the debug console,
 * saving the config to flash, fault monitoring and the status LED.
 */
#include "app.h"
#include "app_config.h"
#include "platform.h"
#include "control_table.h"
#include "config_store.h"
#include "dxl_slave.h"
#include "rs485.h"
#include "fsr_adc.h"
#include "cli.h"

#include "main.h"      /* LED_STATUS_Pin / LED_STATUS_GPIO_Port */
#include <stdio.h>

static void save_config(void)
{
    dev_config_t cfg;
    cfg.id           = ct_get_u8(CT_ADDR_ID);
    cfg.baud_idx     = ct_get_u8(CT_ADDR_BAUD);
    cfg.return_delay = ct_get_u8(CT_ADDR_RETURN_DELAY);

    bool ok = config_save(&cfg);
    ct_set_hw_flag(HW_STATUS_FLASH_ERROR, !ok);
    if (ok) {
        ct_set_hw_flag(HW_STATUS_DEFAULT_CONFIG, false);
    }
}

/* LED: forced on / solid on fault / flicker on bus traffic / 1 Hz heartbeat */
static void led_update(void)
{
    uint32_t now   = plat_millis();
    uint8_t  hw    = ct_get_u8(CT_ADDR_HW_STATUS);
    bool     fault = (hw & (HW_STATUS_ADC_STALLED | HW_STATUS_FLASH_ERROR |
                            HW_STATUS_ADC_START_FAIL)) != 0u;
    bool on;

    if (ct_get_u8(CT_ADDR_LED) != 0u || fault) {
        on = true;
    } else if ((uint32_t)(now - dxl_slave_last_activity_ms()) < 100u) {
        on = ((now / 25u) & 1u) != 0u;
    } else {
        on = (now % 1000u) < 100u;
    }
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void app_init(void)
{
    plat_init();

    dev_config_t cfg;
    bool stored = config_load(&cfg);
    ct_init(cfg.id, cfg.baud_idx, cfg.return_delay);
    ct_set_hw_flag(HW_STATUS_DEFAULT_CONFIG, !stored);

    cli_init();
    dxl_slave_init();

    if (!fsr_adc_start()) {
        ct_set_hw_flag(HW_STATUS_ADC_START_FAIL, true);
    }

    rs485_init(cfg.baud_idx);

    printf("\r\nFoot sensor board fw %u  ID %u  baud %lu%s\r\n",
           (unsigned)FW_VERSION, cfg.id, (unsigned long)rs485_baud_from_index(cfg.baud_idx),
           stored ? "" : "  (defaults, nothing saved yet)");
    printf("type 'help'\r\n> ");
    fflush(stdout);
}

void app_loop(void)
{
    cli_poll();

    if (dxl_slave_take_config_dirty()) {
        save_config();
    }

    if (dxl_slave_reboot_requested()) {
        if (dxl_slave_take_config_dirty()) {
            save_config();
        }
        HAL_Delay(2);
        NVIC_SystemReset();
    }

    uint32_t now = plat_millis();
    ct_set_hw_flag(HW_STATUS_ADC_STALLED,
                   (uint32_t)(now - fsr_adc_last_sample_ms()) > ADC_STALL_TIMEOUT_MS);

    led_update();
}
