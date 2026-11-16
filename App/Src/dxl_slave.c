/**
 * dxl_slave.c - Dynamixel Protocol 2.0 device behaviour.
 *
 * Runs entirely inside the RS485 interrupts (priority 0), so a request is
 * answered within microseconds regardless of what the main loop is doing.
 */
#include "dxl_slave.h"
#include "dxl_protocol.h"
#include "control_table.h"
#include "rs485.h"
#include "app_config.h"
#include "platform.h"

#define MAX_READ_LEN  128u

static dxl_parser_t parser;
static dxl_packet_t pkt;
static uint8_t      reply_data[MAX_READ_LEN];

/* Actions that must wait until our reply has left the wire. */
static volatile bool    post_baud_pending;
static volatile uint8_t post_baud;
static volatile bool    post_reboot;

static volatile bool     config_dirty;
static volatile bool     reboot_request;
static volatile uint32_t last_activity_ms;

void dxl_slave_init(void)
{
    dxl_parser_reset(&parser);
    post_baud_pending = false;
    post_reboot       = false;
    config_dirty      = false;
    reboot_request    = false;
    last_activity_ms  = 0u;
}

static void run_post_actions(void)
{
    if (post_baud_pending) {
        post_baud_pending = false;
        rs485_set_baud(post_baud);
    }
    if (post_reboot) {
        post_reboot    = false;
        reboot_request = true;          /* main loop saves config, then resets */
    }
}

static void schedule_effects(const ct_write_effects_t *fx)
{
    if (fx->config_changed) {
        config_dirty = true;
    }
    if (fx->baud_changed) {
        post_baud         = fx->new_baud;
        post_baud_pending = true;
    }
}

static void factory_reset(uint8_t mode)
{
    /* 0xFF: reset all, 0x01: keep ID, 0x02: keep ID and baud (ROBOTIS) */
    uint8_t id   = ct_get_u8(CT_ADDR_ID);
    uint8_t baud = ct_get_u8(CT_ADDR_BAUD);
    if (mode == 0xFFu) {
        id   = (uint8_t)DEFAULT_DXL_ID;
        baud = (uint8_t)DEFAULT_BAUD_INDEX;
    } else if (mode == 0x01u) {
        baud = (uint8_t)DEFAULT_BAUD_INDEX;
    }
    if (baud != ct_get_u8(CT_ADDR_BAUD)) {
        post_baud         = baud;
        post_baud_pending = true;
    }
    ct_set_u8(CT_ADDR_ID, id);
    ct_set_u8(CT_ADDR_BAUD, baud);
    ct_set_u8(CT_ADDR_RETURN_DELAY, (uint8_t)DEFAULT_RETURN_DELAY);
    ct_set_u8(CT_ADDR_STATUS_RETURN, (uint8_t)DEFAULT_STATUS_RETURN);
    ct_set_u8(CT_ADDR_LED, 0u);
    config_dirty = true;
}

static void send_status(uint8_t id, uint8_t err, const uint8_t *data, uint16_t n)
{
    size_t max;
    uint8_t *tx = rs485_tx_buffer(&max);
    size_t len = dxl_build_status(id, err, data, n, tx, max);
    if (len > 0u) {
        uint32_t delay_us = 2u * (uint32_t)ct_get_u8(CT_ADDR_RETURN_DELAY);
        rs485_start_tx(len, delay_us);
    } else {
        run_post_actions();             /* nothing sent, apply now */
    }
}

static void handle_packet(const dxl_packet_t *p, bool broadcast)
{
    const uint8_t srl = ct_get_u8(CT_ADDR_STATUS_RETURN);
    uint8_t  err   = DXL_ERR_NONE;
    uint16_t n     = 0u;
    bool     reply = !broadcast;

    switch (p->inst) {
    case DXL_INST_PING: {
        if (broadcast) {
            reply = (DXL_ENABLE_BROADCAST_PING != 0);
        }
        uint16_t model = (uint16_t)FW_MODEL_NUMBER;
        reply_data[0] = (uint8_t)(model & 0xFFu);
        reply_data[1] = (uint8_t)(model >> 8);
        reply_data[2] = (uint8_t)FW_VERSION;
        n = 3u;
        break;
    }

    case DXL_INST_READ: {
        if (p->nparams != 4u) {
            err = DXL_ERR_DATA_LENGTH;
        } else {
            uint16_t addr = (uint16_t)(p->params[0] | ((uint16_t)p->params[1] << 8));
            uint16_t len  = (uint16_t)(p->params[2] | ((uint16_t)p->params[3] << 8));
            if (len > MAX_READ_LEN) {
                err = DXL_ERR_DATA_LENGTH;
            } else {
                err = ct_read(addr, len, reply_data);
                if (err == DXL_ERR_NONE) {
                    n = len;
                }
            }
        }
        if (srl < 1u) {
            reply = false;
        }
        break;
    }

    case DXL_INST_WRITE: {
        if (p->nparams < 3u) {
            err = DXL_ERR_DATA_LENGTH;
        } else {
            uint16_t addr = (uint16_t)(p->params[0] | ((uint16_t)p->params[1] << 8));
            ct_write_effects_t fx;
            err = ct_write(addr, &p->params[2], (uint16_t)(p->nparams - 2u), &fx);
            if (err == DXL_ERR_NONE) {
                schedule_effects(&fx);
            }
        }
        /* SRL is read before the write, so changing SRL takes effect next time */
        if (srl < 2u) {
            reply = false;
        }
        break;
    }

    case DXL_INST_REBOOT:
        post_reboot = true;
        if (srl < 2u) {
            reply = false;
        }
        break;

    case DXL_INST_FACTORY_RESET:
        if (p->nparams != 1u ||
            (p->params[0] != 0xFFu && p->params[0] != 0x01u && p->params[0] != 0x02u)) {
            err = DXL_ERR_DATA_RANGE;
        } else {
            factory_reset(p->params[0]);
        }
        if (srl < 2u) {
            reply = false;
        }
        break;

    case DXL_INST_STATUS:          /* another device's reply - never answer */
    case DXL_INST_SYNC_READ:
    case DXL_INST_SYNC_WRITE:
    case DXL_INST_FAST_SYNC_READ:
    case DXL_INST_BULK_READ:
    case DXL_INST_BULK_WRITE:
        return;

    default:
        err = DXL_ERR_INSTRUCTION;
        break;
    }

    if (reply) {
        /* Reply with the ID the request used (old ID if it was just changed) */
        send_status(p->id, err, reply_data, (err == DXL_ERR_NONE) ? n : 0u);
    } else {
        run_post_actions();
    }
}

void dxl_slave_rx_byte(uint8_t b)
{
    if (!dxl_parser_feed(&parser, b)) {
        return;
    }

    const uint8_t id = dxl_parser_id(&parser);
    const uint8_t my = ct_get_u8(CT_ADDR_ID);
    if (id != my && id != DXL_BROADCAST_ID) {
        return;                         /* someone else's packet: skip CRC work */
    }

    if (!dxl_decode(&parser, &pkt)) {
        ct_inc_u16(CT_ADDR_CRC_ERRORS);
        if (id == my && pkt.inst != DXL_INST_STATUS) {
            send_status(my, DXL_ERR_CRC, reply_data, 0u);
        }
        return;
    }

    if (pkt.inst == DXL_INST_STATUS) {
        return;
    }

    ct_inc_u16(CT_ADDR_RX_PACKETS);
    if (id == my) {
        last_activity_ms = plat_millis();
    }
    handle_packet(&pkt, id == DXL_BROADCAST_ID);
}

void dxl_slave_rx_idle(void)
{
    /* The line went quiet in the middle of a packet: it was truncated or the
     * length field was corrupt. Drop it so the next header is found. */
    if (dxl_parser_busy(&parser)) {
        dxl_parser_reset(&parser);
    }
}

void dxl_slave_tx_done(void)
{
    run_post_actions();
}

bool dxl_slave_take_config_dirty(void)
{
    bool d = false;
    CRIT_ENTER();
    d = config_dirty;
    config_dirty = false;
    CRIT_EXIT();
    return d;
}

void dxl_slave_mark_config_dirty(void)
{
    config_dirty = true;
}

bool dxl_slave_reboot_requested(void)
{
    return reboot_request;
}

uint32_t dxl_slave_last_activity_ms(void)
{
    return last_activity_ms;
}
