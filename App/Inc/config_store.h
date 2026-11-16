/**
 * config_store.h - persistent ID / baud / return delay in the last flash page.
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t id;
    uint8_t baud_idx;
    uint8_t return_delay;
} dev_config_t;

/** Fills cfg from flash. Returns false (and fills defaults) if none stored. */
bool config_load(dev_config_t *cfg);

/** Erase + program the config page. Skips the write if nothing changed.
 *  Blocks for ~20 ms during erase (the CPU stalls on flash access). */
bool config_save(const dev_config_t *cfg);

#endif /* CONFIG_STORE_H */
