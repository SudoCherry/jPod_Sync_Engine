#ifndef DATABASE_H
#define DATABASE_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define DATABASE_OK            0
#define DATABASE_ERR_NULL     -1   // NULL pointer passed in
#define DATABASE_ERR_MEMORY   -2   // allocation failed
#define DATABASE_ERR_IO       -3   // file could not be opened / read
#define DATABASE_ERR_FORMAT   -4   // file is not a valid iTunesDB

#define DB_STR_MAX 256

// One track as recorded in the iPod's database
typedef struct {
    uint32_t id;                  // the iPod's unique id for the track
    char title[DB_STR_MAX];       // UTF-8
    char artist[DB_STR_MAX];
    char album[DB_STR_MAX];
    char path[DB_STR_MAX];        // location on the iPod, e.g. /iPod_Control/Music/F00/ABCD.mp3
} db_track_t;

// The tracks found in one iTunesDB file
typedef struct {
    db_track_t *tracks;
    size_t count;
    size_t capacity;
} database_t;

int  database_init(database_t *db);
void database_free(database_t *db);

// READ-ONLY: parse an iTunesDB file (normally <ipod>/iPod_Control/iTunes/iTunesDB)
// and fill db with its tracks. Replaces any tracks already in db.
int  database_load(database_t *db, const char *itunesdb_path);

// READ-ONLY: print the file's structure to `out` (header fields, section types,
// a hex dump of the first track's header, which mhod types occur...).
// Used to see what a real iPod's database looks like.
int  database_inspect(const char *itunesdb_path, FILE *out);

#endif
