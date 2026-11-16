/**
 * config_store.c - two double-words at CFG_FLASH_ADDR:
 *   dw0 = magic(32) | id << 32 | baud << 40 | return_delay << 48 | version << 56
 *   dw1 = ~dw0   (integrity check)
 */
#include "config_store.h"
#include "app_config.h"
#include "main.h"

#define CFG_MAGIC    0x46534231u   /* "FSB1" */
#define CFG_VERSION  1u

/* From the CubeMX linker script: end of initialised data in flash. */
extern uint8_t _sidata[];
extern uint8_t _sdata[];
extern uint8_t _edata[];

static void set_defaults(dev_config_t *cfg)
{
    cfg->id           = (uint8_t)DEFAULT_DXL_ID;
    cfg->baud_idx     = (uint8_t)DEFAULT_BAUD_INDEX;
    cfg->return_delay = (uint8_t)DEFAULT_RETURN_DELAY;
}

static uint64_t pack(const dev_config_t *cfg)
{
    return (uint64_t)CFG_MAGIC |
           ((uint64_t)cfg->id << 32) |
           ((uint64_t)cfg->baud_idx << 40) |
           ((uint64_t)cfg->return_delay << 48) |
           ((uint64_t)CFG_VERSION << 56);
}

static bool read_stored(dev_config_t *cfg)
{
    const volatile uint64_t *p = (const volatile uint64_t *)CFG_FLASH_ADDR;
    uint64_t dw0 = p[0];
    uint64_t dw1 = p[1];

    if (dw1 != ~dw0 || (uint32_t)dw0 != CFG_MAGIC || (uint8_t)(dw0 >> 56) != CFG_VERSION) {
        return false;
    }
    cfg->id           = (uint8_t)(dw0 >> 32);
    cfg->baud_idx     = (uint8_t)(dw0 >> 40);
    cfg->return_delay = (uint8_t)(dw0 >> 48);

    /* Reject out-of-range values rather than trusting them */
    return cfg->id <= 252u && cfg->baud_idx <= 7u && cfg->return_delay <= 254u;
}

bool config_load(dev_config_t *cfg)
{
    if (read_stored(cfg)) {
        return true;
    }
    set_defaults(cfg);
    return false;
}

bool config_save(const dev_config_t *cfg)
{
    dev_config_t current;
    if (read_stored(&current) && current.id == cfg->id &&
        current.baud_idx == cfg->baud_idx && current.return_delay == cfg->return_delay) {
        return true;    /* already stored, spare the flash */
    }

    /* Safety: never erase a page that holds firmware. If this trips, the
     * linker script was not shrunk to 126K (see integration notes). */
    uint32_t fw_end = (uint32_t)_sidata + (uint32_t)(_edata - _sdata);
    if (fw_end > CFG_FLASH_ADDR) {
        return false;
    }

    uint64_t dw0 = pack(cfg);
    uint64_t dw1 = ~dw0;
    bool ok = true;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_1;
    erase.Page      = CFG_FLASH_PAGE;
    erase.NbPages   = 1u;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        ok = false;
    }
    if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, CFG_FLASH_ADDR, dw0) != HAL_OK) {
        ok = false;
    }
    if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, CFG_FLASH_ADDR + 8u, dw1) != HAL_OK) {
        ok = false;
    }
    HAL_FLASH_Lock();

    if (ok) {
        dev_config_t check;
        ok = read_stored(&check) && check.id == cfg->id &&
             check.baud_idx == cfg->baud_idx && check.return_delay == cfg->return_delay;
    }
    return ok;
}
