#ifndef SKEETS_PATHS_H
#define SKEETS_PATHS_H

/* Runtime-resolved root directory for app data.
 * Defaults to /mnt/onboard/.adds/sKeets and may be overridden with
 * the SKEETS_DATA_DIR environment variable. */
const char *skeets_data_dir();

/* Resolved config file path beneath the active data directory. */
const char *skeets_config_path();

/* Resolved image cache directory beneath the active data directory. */
const char *skeets_cache_dir();

/* Ensure the app data directory tree exists.
 * Returns 0 on success, -1 on failure. */
int skeets_ensure_data_dirs();

#endif /* SKEETS_PATHS_H */