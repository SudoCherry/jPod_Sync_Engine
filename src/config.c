#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

#define KEY_MUSIC "music_folder="

int config_init(config_t *cfg) {
    if (cfg == NULL) return CONFIG_ERR_NULL;
    memset(cfg, 0, sizeof(*cfg));
    return CONFIG_OK;
}

// Settings live in the home folder, so they survive no matter where you run the program from
static void default_path(char *out, size_t size) {
    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        snprintf(out, size, "%s/.untitled_sync_engine.conf", home);
    } else {
        snprintf(out, size, ".untitled_sync_engine.conf");
    }
}

int config_load_from(config_t *cfg, const char *path) {
    if (cfg == NULL || path == NULL) return CONFIG_ERR_NULL;
    config_init(cfg);

    FILE *f = fopen(path, "r");
    if (f == NULL) return CONFIG_ERR_IO;

    char line[CONFIG_PATH_MAX + 64];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, KEY_MUSIC, strlen(KEY_MUSIC)) == 0) {
            snprintf(cfg->music_folder, sizeof(cfg->music_folder), "%s", line + strlen(KEY_MUSIC));
        }
    }
    fclose(f);
    return CONFIG_OK;
}

int config_save_to(const config_t *cfg, const char *path) {
    if (cfg == NULL || path == NULL) return CONFIG_ERR_NULL;

    // Write to a temporary file, then rename it over the real one, so a crash
    // half-way through can never leave a damaged settings file behind.
    char tmp[CONFIG_PATH_MAX + 16];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (f == NULL) return CONFIG_ERR_IO;

    int ok = fprintf(f, KEY_MUSIC "%s\n", cfg->music_folder) > 0;
    ok = (fclose(f) == 0) && ok;

    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return CONFIG_ERR_IO;
    }
    return CONFIG_OK;
}

int config_load(config_t *cfg) {
    char path[CONFIG_PATH_MAX];
    default_path(path, sizeof(path));
    return config_load_from(cfg, path);
}

int config_save(const config_t *cfg) {
    char path[CONFIG_PATH_MAX];
    default_path(path, sizeof(path));
    return config_save_to(cfg, path);
}
