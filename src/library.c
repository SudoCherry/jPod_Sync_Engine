#define _POSIX_C_SOURCE 200809L   // for lstat() under -std=c11

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "library.h"
#include "tags.h"

int library_init(library_t *lib) {
    if (lib == NULL) return LIBRARY_ERR_NULL;
    lib->tracks = NULL;
    lib->count = 0;
    lib->capacity = 0;
    return LIBRARY_OK;
}

void library_free(library_t *lib) {
    if (lib == NULL) return;
    free(lib->tracks);
    lib->tracks = NULL;
    lib->count = 0;
    lib->capacity = 0;
}

// Add one track, doubling the array when it is full
static int library_add(library_t *lib, const char *path, long size) {
    if (lib->count == lib->capacity) {
        size_t new_cap = lib->capacity == 0 ? 64 : lib->capacity * 2;
        track_t *bigger = realloc(lib->tracks, new_cap * sizeof(track_t));
        if (bigger == NULL) return LIBRARY_ERR_MEMORY;
        lib->tracks = bigger;
        lib->capacity = new_cap;
    }
    track_t *t = &lib->tracks[lib->count++];
    snprintf(t->path, sizeof(t->path), "%s", path);
    t->size = size;

    // Prefer the real tags. A file with no tags at all gets its details guessed
    // from the file name; a file with some tags but no title just borrows a title.
    tags_t tg, guess;
    int rc = tags_read(path, &tg);
    tags_from_filename(path, &guess);
    if (rc != TAGS_OK) {
        tg = guess;
    } else if (tg.title[0] == '\0') {
        snprintf(tg.title, sizeof(tg.title), "%s", guess.title);
    }
    snprintf(t->title, sizeof(t->title), "%s", tg.title);
    snprintf(t->artist, sizeof(t->artist), "%s", tg.artist);
    snprintf(t->album, sizeof(t->album), "%s", tg.album);
    return LIBRARY_OK;
}

// 1 if the filename ends in a supported audio extension (case-insensitive)
static int is_audio_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return 0;
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".m4a") == 0;
}

int library_scan(library_t *lib, const char *folder) {
    if (lib == NULL || folder == NULL) return LIBRARY_ERR_NULL;

    DIR *dir = opendir(folder);
    if (dir == NULL) return LIBRARY_ERR_FOLDER;

    int result = LIBRARY_OK;
    struct dirent *entry;

    while (result == LIBRARY_OK && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[TRACK_PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;   // path too long: skip

        struct stat st;
        if (lstat(path, &st) != 0) continue;   // lstat: don't follow symlinks

        if (S_ISDIR(st.st_mode)) {
            int sub = library_scan(lib, path);
            if (sub == LIBRARY_ERR_MEMORY) result = sub;   // other errors: skip folder
        } else if (S_ISREG(st.st_mode) && is_audio_file(entry->d_name)) {
            result = library_add(lib, path, (long)st.st_size);
        }
    }

    closedir(dir);
    return result;
}
