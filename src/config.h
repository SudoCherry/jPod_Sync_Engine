#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_OK            0
#define CONFIG_ERR_NULL     -1   // NULL pointer passed in
#define CONFIG_ERR_IO       -2   // file missing, or could not be written

#define CONFIG_PATH_MAX 1024

// Settings that are remembered between runs
typedef struct {
    char music_folder[CONFIG_PATH_MAX];   // empty string = not set yet
} config_t;

// Reset to "nothing set"
int config_init(config_t *cfg);

// Load / save using the default settings file (~/.untitled_sync_engine.conf).
// A missing file on load is normal on the first run: it returns CONFIG_ERR_IO
// and leaves cfg empty.
int config_load(config_t *cfg);
int config_save(const config_t *cfg);

// Same, but with an explicit file path (used by the tests)
int config_load_from(config_t *cfg, const char *path);
int config_save_to(const config_t *cfg, const char *path);

#endif
