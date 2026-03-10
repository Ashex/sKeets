#include "config.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_MAX_ENTRIES 64

typedef struct {
    char key[CONFIG_MAX_KEY];
    char value[CONFIG_MAX_VALUE];
} config_entry_t;

struct config {
    char            path[512];
    config_entry_t  entries[CONFIG_MAX_ENTRIES];
    int             count;
};

static config_entry_t *find_entry(config_t *cfg, const char *key) {
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return &cfg->entries[i];
    }
    return NULL;
}

config_t *config_open(const char *path) {
    config_t *cfg = (config_t *)calloc(1, sizeof(config_t));
    if (!cfg) return NULL;
    str_safe_copy(cfg->path, path, sizeof(cfg->path));

    FILE *f = fopen(path, "r");
    if (!f) return cfg; /* new config, no file yet */

    char line[CONFIG_MAX_KEY + CONFIG_MAX_VALUE + 4];
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line;
        char *v = eq + 1;
        str_trim(k);
        str_trim(v);
        if (str_empty(k)) continue;
        config_set_str(cfg, k, v);
    }
    fclose(f);
    return cfg;
}

void config_free(config_t *cfg) {
    free(cfg);
}

int config_save(config_t *cfg) {
    if (!cfg) return -1;
    FILE *f = fopen(cfg->path, "w");
    if (!f) return -1;
    fprintf(f, "# sKeets config\n");
    for (int i = 0; i < cfg->count; i++)
        fprintf(f, "%s=%s\n", cfg->entries[i].key, cfg->entries[i].value);
    fclose(f);
    return 0;
}

const char *config_get_str(const config_t *cfg, const char *key, const char *def) {
    if (!cfg || !key) return def;
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].value;
    }
    return def;
}

int config_get_int(const config_t *cfg, const char *key, int def) {
    const char *v = config_get_str(cfg, key, NULL);
    if (!v) return def;
    return atoi(v);
}

bool config_get_bool(const config_t *cfg, const char *key, bool def) {
    const char *v = config_get_str(cfg, key, NULL);
    if (!v) return def;
    return (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "yes") == 0);
}

void config_set_str(config_t *cfg, const char *key, const char *value) {
    if (!cfg || !key || !value) return;
    config_entry_t *e = find_entry(cfg, key);
    if (e) {
        str_safe_copy(e->value, value, CONFIG_MAX_VALUE);
        return;
    }
    if (cfg->count >= CONFIG_MAX_ENTRIES) return;
    e = &cfg->entries[cfg->count++];
    str_safe_copy(e->key,   key,   CONFIG_MAX_KEY);
    str_safe_copy(e->value, value, CONFIG_MAX_VALUE);
}

void config_set_int(config_t *cfg, const char *key, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    config_set_str(cfg, key, buf);
}

void config_set_bool(config_t *cfg, const char *key, bool value) {
    config_set_str(cfg, key, value ? "true" : "false");
}
