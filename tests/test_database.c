#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "database.h"

// ---- helpers that build a small iTunesDB in memory ----
// NOTE: this tests the parser against OUR reading of the format.
// It does not prove it matches a real iPod's file.

typedef struct { uint8_t d[8192]; size_t n; } buf_t;

static void put32(buf_t *b, uint32_t v) {
    for (int i = 0; i < 4; i++) b->d[b->n++] = (uint8_t)(v >> (8 * i));
}
static void put_tag(buf_t *b, const char *t) { memcpy(b->d + b->n, t, 4); b->n += 4; }
static void pad_to(buf_t *b, size_t start, size_t hdr) { while (b->n < start + hdr) b->d[b->n++] = 0; }
static void patch32(buf_t *b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b->d[off + i] = (uint8_t)(v >> (8 * i));
}

static void add_mhod(buf_t *b, uint32_t type, const uint16_t *u, size_t nunits) {
    size_t xl = nunits * 2;
    put_tag(b, "mhod"); put32(b, 24); put32(b, (uint32_t)(40 + xl)); put32(b, type);
    put32(b, 0); put32(b, 0);                       // pad header to 24
    put32(b, 1); put32(b, (uint32_t)xl); put32(b, 0); put32(b, 0);
    for (size_t i = 0; i < nunits; i++) { b->d[b->n++] = (uint8_t)u[i]; b->d[b->n++] = (uint8_t)(u[i] >> 8); }
}
static void add_ascii_mhod(buf_t *b, uint32_t type, const char *s) {
    uint16_t u[128]; size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) u[i] = (uint8_t)s[i];
    add_mhod(b, type, u, n);
}
static void add_mhit(buf_t *b, uint32_t id, const uint16_t *title, size_t tn,
                     const char *artist, const char *album, const char *path) {
    size_t start = b->n;
    put_tag(b, "mhit"); put32(b, 156); put32(b, 0); put32(b, 4); put32(b, id);
    pad_to(b, start, 156);
    add_mhod(b, 1, title, tn);
    add_ascii_mhod(b, 4, artist);
    add_ascii_mhod(b, 3, album);
    add_ascii_mhod(b, 2, path);
    patch32(b, start + 8, (uint32_t)(b->n - start));
}

static size_t build_db(buf_t *b) {
    b->n = 0;
    put_tag(b, "mhbd"); put32(b, 104); put32(b, 0);
    pad_to(b, 0, 104);

    size_t sd = b->n;                                // track-list dataset
    put_tag(b, "mhsd"); put32(b, 96); put32(b, 0); put32(b, 1);
    pad_to(b, sd, 96);
    size_t lt = b->n;
    put_tag(b, "mhlt"); put32(b, 92); put32(b, 2);
    pad_to(b, lt, 92);

    const uint16_t t1[] = { 'C', 'a', 'f', 0x00E9 };          // "Café"
    const uint16_t t2b[] = { 'S', 'o', 'n', 'g', ' ', 0xD83C, 0xDFB5 };  // "Song 🎵"
    add_mhit(b, 7,  t1,  4, "Artist One", "Album One", ":iPod_Control:Music:F00:AAAA.mp3");
    add_mhit(b, 42, t2b, 7, "Artist Two", "Album Two", ":iPod_Control:Music:F01:BBBB.m4a");
    patch32(b, sd + 8, (uint32_t)(b->n - sd));

    size_t sp = b->n;                                // playlist dataset (must be skipped)
    put_tag(b, "mhsd"); put32(b, 96); put32(b, 0); put32(b, 2);
    pad_to(b, sp, 96);
    size_t lp = b->n;
    put_tag(b, "mhlp"); put32(b, 92); put32(b, 1);                // one playlist
    pad_to(b, lp, 92);
    size_t yp = b->n;
    put_tag(b, "mhyp"); put32(b, 48); put32(b, 48 + 76); put32(b, 0); put32(b, 1);   // 0 mhods, 1 mhip
    pad_to(b, yp, 48);
    size_t ip = b->n;
    put_tag(b, "mhip"); put32(b, 76); put32(b, 76);
    pad_to(b, ip, 76);
    patch32(b, sp + 8, (uint32_t)(b->n - sp));

    patch32(b, 8, (uint32_t)b->n);
    return b->n;
}

static void write_file(const char *path, const uint8_t *d, size_t n) {
    FILE *f = fopen(path, "wb"); assert(f);
    fwrite(d, 1, n, f); fclose(f);
}

int main(void) {
    buf_t b; size_t n = build_db(&b);
    const char *path = "/tmp/test_itunesdb.bin";
    write_file(path, b.d, n);

    database_t db;
    assert(database_init(&db) == DATABASE_OK);

    // --- good file ---
    assert(database_load(&db, path) == DATABASE_OK);
    assert(db.count == 2);
    assert(db.tracks[0].id == 7);
    assert(strcmp(db.tracks[0].title, "Caf\xC3\xA9") == 0);                 // é -> 2 UTF-8 bytes
    assert(strcmp(db.tracks[0].artist, "Artist One") == 0);
    assert(strcmp(db.tracks[0].album, "Album One") == 0);
    assert(strcmp(db.tracks[0].path, "/iPod_Control/Music/F00/AAAA.mp3") == 0);
    assert(db.tracks[1].id == 42);
    assert(strcmp(db.tracks[1].title, "Song \xF0\x9F\x8E\xB5") == 0);        // surrogate pair -> 4 bytes

    // --- inspector ---
    FILE *cap = tmpfile(); assert(cap);
    assert(database_inspect(path, cap) == DATABASE_OK);
    rewind(cap);
    char text[16384]; size_t tn = fread(text, 1, sizeof text - 1, cap); text[tn] = '\0';
    fclose(cap);
    assert(strstr(text, "mhsd #0: type=1") != NULL);
    assert(strstr(text, "tracks read: 2 of 2") != NULL);
    assert(strstr(text, "[type 1: 2]") != NULL);                            // two titles
    assert(strstr(text, "mhbd header bytes:") != NULL);
    assert(strstr(text, "first playlist: mhyp") != NULL);
    assert(strstr(text, "first playlist item (mhip): mhip") != NULL);
    assert(database_inspect("/no/such/file", stdout) == DATABASE_ERR_IO);
    assert(database_inspect(NULL, stdout) == DATABASE_ERR_NULL);

    // --- error paths ---
    assert(database_load(&db, "/no/such/file") == DATABASE_ERR_IO);
    assert(database_load(NULL, path) == DATABASE_ERR_NULL);

    // --- damaged files must fail cleanly, never crash ---
    FILE *devnull = fopen("/dev/null", "w"); assert(devnull);
    srand(12345);
    for (int i = 0; i < 3000; i++) {
        buf_t c = b; size_t cn = n;
        int flips = 1 + rand() % 4;
        for (int k = 0; k < flips; k++) c.d[rand() % cn] = (uint8_t)rand();
        if (rand() % 3 == 0) cn = (size_t)rand() % cn;                      // also truncate
        write_file("/tmp/test_itunesdb_bad.bin", c.d, cn);
        int rc = database_load(&db, "/tmp/test_itunesdb_bad.bin");
        assert(rc == DATABASE_OK || rc == DATABASE_ERR_FORMAT || rc == DATABASE_ERR_IO);
        rc = database_inspect("/tmp/test_itunesdb_bad.bin", devnull);          // inspector must be just as safe
        assert(rc == DATABASE_OK || rc == DATABASE_ERR_FORMAT || rc == DATABASE_ERR_IO);
    }

    fclose(devnull);
    database_free(&db);
    database_free(&db);                                                      // double free is safe
    puts("database tests passed");
    return 0;
}
