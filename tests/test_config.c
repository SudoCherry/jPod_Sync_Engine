#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "config.h"

int main(void) {
    const char *path = "/tmp/test_sync_config.conf";
    remove(path);

    config_t cfg;
    assert(config_init(&cfg) == CONFIG_OK);
    assert(cfg.music_folder[0] == '\0');

    // first run: no file yet
    assert(config_load_from(&cfg, path) == CONFIG_ERR_IO);
    assert(cfg.music_folder[0] == '\0');

    // save, then load back (path with a space in it)
    snprintf(cfg.music_folder, sizeof cfg.music_folder, "/Users/someone/Downloads/Test Music");
    assert(config_save_to(&cfg, path) == CONFIG_OK);

    config_t loaded;
    assert(config_load_from(&loaded, path) == CONFIG_OK);
    assert(strcmp(loaded.music_folder, "/Users/someone/Downloads/Test Music") == 0);

    // saving again replaces the old value
    snprintf(cfg.music_folder, sizeof cfg.music_folder, "/other");
    assert(config_save_to(&cfg, path) == CONFIG_OK);
    assert(config_load_from(&loaded, path) == CONFIG_OK);
    assert(strcmp(loaded.music_folder, "/other") == 0);

    // error paths
    assert(config_save_to(&cfg, "/no/such/dir/x.conf") == CONFIG_ERR_IO);
    assert(config_init(NULL) == CONFIG_ERR_NULL);
    assert(config_load_from(NULL, path) == CONFIG_ERR_NULL);
    assert(config_save_to(NULL, path) == CONFIG_ERR_NULL);

    remove(path);
    puts("config tests passed");
    return 0;
}
