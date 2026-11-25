#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "tags.h"

// NOTE: these files are built by hand from the ID3 specs, so they test that the
// reader matches OUR reading of the format. Real MP3s are the real test.

typedef struct { uint8_t d[8192]; size_t n; } buf_t;

static void put(buf_t *b, const void *p, size_t n) { memcpy(b->d + b->n, p, n); b->n += n; }
static void put8(buf_t *b, uint8_t v) { b->d[b->n++] = v; }
static void put_be32(buf_t *b, uint32_t v) { put8(b, v >> 24); put8(b, v >> 16); put8(b, v >> 8); put8(b, v); }
static void put_be24(buf_t *b, uint32_t v) { put8(b, v >> 16); put8(b, v >> 8); put8(b, v); }
static void put_syncsafe(buf_t *b, uint32_t v) {
    put8(b, (v >> 21) & 0x7F); put8(b, (v >> 14) & 0x7F); put8(b, (v >> 7) & 0x7F); put8(b, v & 0x7F);
}

static void frame22(buf_t *b, const char *id, const uint8_t *d, size_t n) { put(b, id, 3); put_be24(b, (uint32_t)n); put(b, d, n); }
static void frame23(buf_t *b, const char *id, const uint8_t *d, size_t n) { put(b, id, 4); put_be32(b, (uint32_t)n); put8(b, 0); put8(b, 0); put(b, d, n); }
static void frame24(buf_t *b, const char *id, const uint8_t *d, size_t n) { put(b, id, 4); put_syncsafe(b, (uint32_t)n); put8(b, 0); put8(b, 0); put(b, d, n); }

// header + body + fake "audio" -> file
static void write_mp3(const char *path, unsigned major, unsigned flags, const buf_t *body, size_t padding, int unsync) {
    buf_t out = { .n = 0 };
    buf_t b = *body;
    for (size_t i = 0; i < padding; i++) b.d[b.n++] = 0;

    if (unsync) {                                   // insert 00 after every FF
        buf_t u = { .n = 0 };
        for (size_t i = 0; i < b.n; i++) { put8(&u, b.d[i]); if (b.d[i] == 0xFF) put8(&u, 0x00); }
        b = u;
    }
    put(&out, "ID3", 3); put8(&out, (uint8_t)major); put8(&out, 0); put8(&out, (uint8_t)flags);
    put_syncsafe(&out, (uint32_t)b.n);
    put(&out, b.d, b.n);
    put(&out, "\xFF\xFB\x90\x00audio-bytes", 15);

    FILE *f = fopen(path, "wb"); assert(f);
    fwrite(out.d, 1, out.n, f); fclose(f);
}

static void check(const char *path, const char *title, const char *artist, const char *album) {
    tags_t t;
    assert(tags_read(path, &t) == TAGS_OK);
    if (strcmp(t.title, title) || strcmp(t.artist, artist) || strcmp(t.album, album)) {
        fprintf(stderr, "MISMATCH %s: got [%s] [%s] [%s]\n", path, t.title, t.artist, t.album);
        assert(0);
    }
}

int main(void) {
    const char *p = "/tmp/test_tags.mp3";
    buf_t body;

    // ---- v2.3: UTF-16 (with BOM), Latin-1, UTF-8 frames ----
    body.n = 0;
    { const uint8_t d[] = {1, 0xFF, 0xFE, 'T', 0, 0xE9, 0, 's', 0, 't', 0};   frame23(&body, "TIT2", d, sizeof d); }  // "Tést"
    { const uint8_t d[] = {0, 'C', 'a', 'f', 0xE9};                            frame23(&body, "TPE1", d, sizeof d); }  // "Café"
    { const uint8_t d[] = {3, 'A', 'l', 'b', 0xC3, 0xA9};                      frame23(&body, "TALB", d, sizeof d); }  // "Albé"
    write_mp3(p, 3, 0, &body, 20, 0);
    check(p, "T\xC3\xA9st", "Caf\xC3\xA9", "Alb\xC3\xA9");

    // ---- v2.4: syncsafe frame sizes, UTF-8, UTF-16BE, emoji surrogate pair ----
    body.n = 0;
    { const uint8_t d[] = {3, 'S', 'o', 'n', 'g'};                                   frame24(&body, "TIT2", d, sizeof d); }
    { const uint8_t d[] = {2, 0x00, 'B', 0x00, 'a', 0x00, 'n', 0x00, 'd'};           frame24(&body, "TPE1", d, sizeof d); }
    { const uint8_t d[] = {2, 0xD8, 0x3C, 0xDF, 0xB5, 0x00, 'x'};                    frame24(&body, "TALB", d, sizeof d); }
    write_mp3(p, 4, 0, &body, 0, 0);
    check(p, "Song", "Band", "\xF0\x9F\x8E\xB5x");

    // ---- v2.4: only the first of several null-separated values is used ----
    body.n = 0;
    { const uint8_t d[] = {3, 'O', 'n', 'e', 0, 'T', 'w', 'o'};                      frame24(&body, "TPE1", d, sizeof d); }
    { const uint8_t d[] = {3, 'T'};                                                  frame24(&body, "TIT2", d, sizeof d); }
    write_mp3(p, 4, 0, &body, 0, 0);
    check(p, "T", "One", "");

    // ---- v2.2: 3-letter frame ids ----
    body.n = 0;
    { const uint8_t d[] = {0, 'O', 'l', 'd'};      frame22(&body, "TT2", d, sizeof d); }
    { const uint8_t d[] = {0, 'A', 'r', 't'};      frame22(&body, "TP1", d, sizeof d); }
    { const uint8_t d[] = {0, 'L', 'P'};           frame22(&body, "TAL", d, sizeof d); }
    write_mp3(p, 2, 0, &body, 0, 0);
    check(p, "Old", "Art", "LP");

    // ---- v2.3 with an extended header ----
    body.n = 0;
    put_be32(&body, 6); put8(&body, 0); put8(&body, 0); put_be32(&body, 0);          // 10-byte extended header
    { const uint8_t d[] = {0, 'E', 'x', 't'};      frame23(&body, "TIT2", d, sizeof d); }
    write_mp3(p, 3, 0x40, &body, 0, 0);
    check(p, "Ext", "", "");

    // ---- unsynchronised tag (FF 00 pairs must be undone) ----
    body.n = 0;
    { const uint8_t d[] = {0, 'a', 0xFF, 'b'};     frame23(&body, "TIT2", d, sizeof d); }   // "aÿb"
    write_mp3(p, 3, 0x80, &body, 0, 1);
    check(p, "a\xC3\xBF" "b", "", "");

    // ---- unknown frames are skipped; a title frame after them is still found ----
    body.n = 0;
    { const uint8_t d[] = {0, 1, 2, 3, 4, 5, 6, 7}; frame23(&body, "APIC", d, sizeof d); }
    { const uint8_t d[] = {0, 'F', 'o', 'u', 'n', 'd'}; frame23(&body, "TIT2", d, sizeof d); }
    write_mp3(p, 3, 0, &body, 0, 0);
    check(p, "Found", "", "");

    // ---- ID3v1 only (last 128 bytes) ----
    {
        buf_t f = { .n = 0 };
        put(&f, "\xFF\xFB\x90\x00audio", 9);
        put(&f, "TAG", 3);
        char fld[34];
        memset(fld, 0, 30); memcpy(fld, "V1 Title", 8);  put(&f, fld, 30);
        memset(fld, ' ', 30); memcpy(fld, "V1 Artist", 9); put(&f, fld, 30);      // space-padded
        memset(fld, 0, 30); memcpy(fld, "V1 Album", 8);  put(&f, fld, 30);
        memset(fld, 0, 34); put(&f, fld, 4); put(&f, fld, 30); put8(&f, 0);       // year, comment, genre
        FILE *fp = fopen(p, "wb"); assert(fp); fwrite(f.d, 1, f.n, fp); fclose(fp);
        check(p, "V1 Title", "V1 Artist", "V1 Album");
    }

    // ---- no tags / missing file / bad arguments ----
    { FILE *fp = fopen(p, "wb"); fputs("just some bytes, no tags here", fp); fclose(fp); }
    tags_t t;
    assert(tags_read(p, &t) == TAGS_ERR_NONE);
    assert(t.title[0] == '\0');
    assert(tags_read("/no/such/file.mp3", &t) == TAGS_ERR_IO);
    assert(tags_read(NULL, &t) == TAGS_ERR_NULL);
    assert(tags_read(p, NULL) == TAGS_ERR_NULL);

    // ---- guessing from file names ----
    tags_from_filename("/Users/x/Test Music/Asylum - mAn is Five.mp3", &t);
    assert(!strcmp(t.artist, "Asylum") && !strcmp(t.title, "mAn is Five"));
    tags_from_filename("Just A Title.m4a", &t);
    assert(!strcmp(t.artist, "") && !strcmp(t.title, "Just A Title"));
    tags_from_filename("/a/b/no_extension", &t);
    assert(!strcmp(t.title, "no_extension"));
    tags_from_filename("/a/Rock - Roll - Part 2.mp3", &t);                    // splits at the first " - "
    assert(!strcmp(t.artist, "Rock") && !strcmp(t.title, "Roll - Part 2"));
    tags_from_filename("/a/- Leading.mp3", &t);
    assert(t.title[0] != '\0');

    // ---- damaged files must be handled safely (run under a sanitizer to be sure) ----
    body.n = 0;
    { const uint8_t d[] = {1, 0xFF, 0xFE, 'T', 0, 'e', 0};  frame23(&body, "TIT2", d, sizeof d); }
    { const uint8_t d[] = {0, 'A', 'r', 't'};               frame23(&body, "TPE1", d, sizeof d); }
    write_mp3(p, 3, 0, &body, 10, 0);
    FILE *fp = fopen(p, "rb"); uint8_t orig[512]; size_t on = fread(orig, 1, sizeof orig, fp); fclose(fp);

    srand(2024);
    for (int i = 0; i < 4000; i++) {
        uint8_t c[512]; memcpy(c, orig, on);
        size_t cn = on;
        int flips = 1 + rand() % 5;
        for (int k = 0; k < flips; k++) c[rand() % (int)cn] = (uint8_t)rand();
        if (rand() % 3 == 0) cn = (size_t)rand() % cn;
        fp = fopen("/tmp/test_tags_bad.mp3", "wb"); fwrite(c, 1, cn, fp); fclose(fp);
        int rc = tags_read("/tmp/test_tags_bad.mp3", &t);
        assert(rc == TAGS_OK || rc == TAGS_ERR_NONE);
        assert(strlen(t.title) < TAG_STR_MAX && strlen(t.artist) < TAG_STR_MAX && strlen(t.album) < TAG_STR_MAX);
    }

    remove(p); remove("/tmp/test_tags_bad.mp3");
    puts("tags tests passed");
    return 0;
}
