#ifndef SKEETS_CONFIG_H
#define SKEETS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_PATH "/mnt/onboard/.adds/sKeets/config.ini"
#define CONFIG_MAX_KEY   64
#define CONFIG_MAX_VALUE 2048

/* Opaque config handle */
typedef struct config config_t;

/* Open/create a config file.  Returns NULL on failure. */
config_t *config_open(const char *path);

/* Free memory held by config; does NOT write to disk. */
void config_free(config_t *cfg);

/* Persist config to disk.  Returns 0 on success. */
int config_save(config_t *cfg);

/* Get a string value. Returns def if key not found. */
const char *config_get_str(const config_t *cfg, const char *key, const char *def);

/* Get an integer value. Returns def if key not found. */
int config_get_int(const config_t *cfg, const char *key, int def);

/* Get a boolean value (1/true/yes → true). Returns def if not found. */
bool config_get_bool(const config_t *cfg, const char *key, bool def);

/* Set / update a string value. */
void config_set_str(config_t *cfg, const char *key, const char *value);

/* Set / update an integer value. */
void config_set_int(config_t *cfg, const char *key, int value);

/* Set / update a boolean value. */
void config_set_bool(config_t *cfg, const char *key, bool value);

#endif /* SKEETS_CONFIG_H */
