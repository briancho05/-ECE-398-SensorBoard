/**
 * control_table.c - register map storage and access rules.
 */
#include "control_table.h"
#include "dxl_protocol.h"
#include "app_config.h"
#include "platform.h"
#include <string.h>

static volatile uint8_t ct[CT_SIZE];

static void put16(uint16_t a, uint16_t v)
{
    ct[a] = (uint8_t)(v & 0xFFu);
    ct[a + 1u] = (uint8_t)(v >> 8);
}

static void put32(uint16_t a, uint32_t v)
{
    ct[a]      = (uint8_t)(v & 0xFFu);
    ct[a + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    ct[a + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    ct[a + 3u] = (uint8_t)(v >> 24);
}

static uint16_t get16(uint16_t a)
{
    return (uint16_t)(ct[a] | ((uint16_t)ct[a + 1u] << 8));
}

static uint32_t get32(uint16_t a)
{
    return (uint32_t)ct[a] | ((uint32_t)ct[a + 1u] << 8) |
           ((uint32_t)ct[a + 2u] << 16) | ((uint32_t)ct[a + 3u] << 24);
}

void ct_init(uint8_t id, uint8_t baud_idx, uint8_t return_delay)
{
    for (uint16_t i = 0; i < CT_SIZE; i++) {
        ct[i] = 0u;
    }
    put16(CT_ADDR_MODEL_NUMBER, (uint16_t)FW_MODEL_NUMBER);
    put32(CT_ADDR_MODEL_INFO, 0u);
    ct[CT_ADDR_FW_VERSION]    = (uint8_t)FW_VERSION;
    ct[CT_ADDR_ID]            = id;
    ct[CT_ADDR_BAUD]          = baud_idx;
    ct[CT_ADDR_RETURN_DELAY]  = return_delay;
    ct[CT_ADDR_LED]           = 0u;
    ct[CT_ADDR_STATUS_RETURN] = (uint8_t)DEFAULT_STATUS_RETURN;
    ct[CT_ADDR_HW_STATUS]     = 0u;
}

/* Which single bytes may be written over the bus, and their valid range. */
static bool writable(uint16_t a)
{
    return a == CT_ADDR_ID || a == CT_ADDR_BAUD || a == CT_ADDR_RETURN_DELAY ||
           a == CT_ADDR_LED || a == CT_ADDR_STATUS_RETURN;
}

static bool in_range(uint16_t a, uint8_t v)
{
    switch (a) {
    case CT_ADDR_ID:            return v <= DXL_MAX_ID;
    case CT_ADDR_BAUD:          return v <= 7u;
    case CT_ADDR_RETURN_DELAY:  return v <= 254u;
    case CT_ADDR_LED:           return v <= 1u;
    case CT_ADDR_STATUS_RETURN: return v <= 2u;
    default:                    return false;
    }
}

uint8_t ct_read(uint16_t addr, uint16_t len, uint8_t *out)
{
    if (len == 0u) {
        return DXL_ERR_DATA_LENGTH;
    }
    if ((uint32_t)addr + len > CT_SIZE) {
        return DXL_ERR_ACCESS;
    }
    /* Called from the highest-priority ISR; the ADC writer uses a critical
     * section, so this copy always sees a consistent sweep. */
    for (uint16_t i = 0; i < len; i++) {
        out[i] = ct[addr + i];
    }
    return DXL_ERR_NONE;
}

uint8_t ct_write(uint16_t addr, const uint8_t *data, uint16_t len, ct_write_effects_t *fx)
{
    fx->config_changed = false;
    fx->baud_changed   = false;
    fx->new_baud       = 0u;

    if (len == 0u) {
        return DXL_ERR_DATA_LENGTH;
    }
    if ((uint32_t)addr + len > CT_SIZE) {
        return DXL_ERR_ACCESS;
    }
    /* Validate everything first: a rejected write changes nothing. */
    for (uint16_t i = 0; i < len; i++) {
        if (!writable((uint16_t)(addr + i))) {
            return DXL_ERR_ACCESS;
        }
    }
    for (uint16_t i = 0; i < len; i++) {
        if (!in_range((uint16_t)(addr + i), data[i])) {
            return DXL_ERR_DATA_RANGE;
        }
    }

    for (uint16_t i = 0; i < len; i++) {
        uint16_t a = (uint16_t)(addr + i);
        uint8_t  v = data[i];
        if (ct[a] == v) {
            continue;
        }
        ct[a] = v;
        if (a == CT_ADDR_ID || a == CT_ADDR_BAUD || a == CT_ADDR_RETURN_DELAY) {
            fx->config_changed = true;
        }
        if (a == CT_ADDR_BAUD) {
            fx->baud_changed = true;
            fx->new_baud     = v;
        }
    }
    return DXL_ERR_NONE;
}

uint8_t ct_get_u8(uint16_t addr)
{
    return ct[addr];
}

uint16_t ct_get_u16(uint16_t addr)
{
    uint16_t v;
    CRIT_ENTER();
    v = get16(addr);
    CRIT_EXIT();
    return v;
}

uint32_t ct_get_u32(uint16_t addr)
{
    uint32_t v;
    CRIT_ENTER();
    v = get32(addr);
    CRIT_EXIT();
    return v;
}

void ct_set_u8(uint16_t addr, uint8_t v)
{
    ct[addr] = v;
}

void ct_inc_u16(uint16_t addr)
{
    CRIT_ENTER();
    put16(addr, (uint16_t)(get16(addr) + 1u));
    CRIT_EXIT();
}

void ct_set_hw_flag(uint8_t mask, bool on)
{
    CRIT_ENTER();
    if (on) {
        ct[CT_ADDR_HW_STATUS] = (uint8_t)(ct[CT_ADDR_HW_STATUS] | mask);
    } else {
        ct[CT_ADDR_HW_STATUS] = (uint8_t)(ct[CT_ADDR_HW_STATUS] & (uint8_t)~mask);
    }
    CRIT_EXIT();
}

void ct_update_fsr(const volatile uint16_t *raw)
{
    CRIT_ENTER();
    for (uint16_t i = 0; i < FSR_COUNT; i++) {
        put16((uint16_t)(CT_ADDR_FSR0 + 2u * i), raw[i]);
    }
    put32(CT_ADDR_SAMPLE_COUNT, get32(CT_ADDR_SAMPLE_COUNT) + 1u);
    CRIT_EXIT();
}

void ct_snapshot_fsr(uint16_t *raw4, uint32_t *count)
{
    CRIT_ENTER();
    for (uint16_t i = 0; i < FSR_COUNT; i++) {
        raw4[i] = get16((uint16_t)(CT_ADDR_FSR0 + 2u * i));
    }
    *count = get32(CT_ADDR_SAMPLE_COUNT);
    CRIT_EXIT();
}
