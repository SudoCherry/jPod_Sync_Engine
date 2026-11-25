#ifndef LIBRARY_H
#define LIBRARY_H

#include <stddef.h>
#include "tags.h"

#define LIBRARY_OK            0
#define LIBRARY_ERR_NULL     -1   // NULL pointer passed in
#define LIBRARY_ERR_MEMORY   -2   // allocation failed
#define LIBRARY_ERR_FOLDER   -3   // folder could not be opened

#define TRACK_PATH_MAX 1024

// One music file found on the computer
typedef struct {
    char path[TRACK_PATH_MAX];
    long size;                    // bytes
    char title[TAG_STR_MAX];      // UTF-8; from the tags, or guessed from the file name
    char artist[TAG_STR_MAX];
    char album[TAG_STR_MAX];
} track_t;

// A growable list of tracks
typedef struct {
    track_t *tracks;
    size_t count;
    size_t capacity;
} library_t;

// Set up an empty library
int library_init(library_t *lib);

// Free the memory a library owns (safe to call twice)
void library_free(library_t *lib);

// Recursively scan a folder, adding every .mp3 / .m4a file found.
// Tracks are appended, so you can scan more than one folder.
int library_scan(library_t *lib, const char *folder);

#endif
