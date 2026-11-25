#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "dbwriter.h"
#include "database.h"
#include "fixture_db.h"     // a tiny made-up database, see make_fixture.py

static void write_file(const char *path, const unsigned char *d, size_t n) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(d, 1, n, f) == n);
    fclose(f);
}

static dbw_track_t base_track(const char *title, const char *artist, const char *album, const char *path) {
    dbw_track_t t;
    memset(&t, 0, sizeof(t));
    t.title = title; t.artist = artist; t.album = album; t.ipod_path = path;
    t.size_bytes = 5000000; t.length_ms = 210000; t.bitrate_kbps = 190; t.sample_rate = 44100;
    t.added_time = dbwriter_mac_time(1790000000); t.modified_time = dbwriter_mac_time(1780000000);
    return t;
}

static int add(const char *in, const char *out, const dbw_track_t *t, dbw_report_t *r) {
    remove(out);
    return dbwriter_add_track(in, out, t, r);
}

static void expect_ok(const char *path) {
    char err[256];
    int rc = dbwriter_verify(path, err, sizeof err);
    if (rc != DBW_OK) { fprintf(stderr, "verify failed: %s\n", err); assert(0); }
}

int main(void) {
    char db0[64], db1[64], db2[64], db3[64], bad[64];
    snprintf(db0, sizeof db0, "/tmp/dbw_%ld_0.db", (long)getpid());
    snprintf(db1, sizeof db1, "/tmp/dbw_%ld_1.db", (long)getpid());
    snprintf(db2, sizeof db2, "/tmp/dbw_%ld_2.db", (long)getpid());
    snprintf(db3, sizeof db3, "/tmp/dbw_%ld_3.db", (long)getpid());
    snprintf(bad, sizeof bad, "/tmp/dbw_%ld_bad.db", (long)getpid());

    write_file(db0, fixture_db, sizeof fixture_db);
    expect_ok(db0);

    dbw_report_t r;
    database_t d; database_init(&d);

    // --- an existing artist and album: no new album record ---
    dbw_track_t t1 = base_track("Foxtrot Run", "Artist One", "First Album", ":iPod_Control:Music:F01:ZZZZ.mp3");
    t1.track_number = 3; t1.year = 2024; t1.vbr = 1; t1.sample_rate = 48000;
    assert(add(db0, db1, &t1, &r) == DBW_OK);
    assert(r.tracks_before == 5 && r.tracks_after == 6);
    assert(r.new_album == 0 && r.new_artist == 0);
    expect_ok(db1);
    assert(database_load(&d, db1) == DATABASE_OK && d.count == 6);
    assert(strcmp(d.tracks[5].title, "Foxtrot Run") == 0 && strcmp(d.tracks[5].artist, "Artist One") == 0);
    assert(strcmp(d.tracks[5].album, "First Album") == 0 && strcmp(d.tracks[5].path, "/iPod_Control/Music/F01/ZZZZ.mp3") == 0);
    assert(d.tracks[5].id == r.track_id && d.tracks[5].id > d.tracks[4].id);

    // --- a new artist and album: one new album record; non-ASCII text survives ---
    dbw_track_t t2 = base_track("Gr\xC3\xBC\xC3\x9F" " Gott \xF0\x9F\x8E\xB5", "New \xC3\x89" "mile", "Nouvel Album", ":iPod_Control:Music:F02:YYYY.mp3");
    assert(add(db1, db2, &t2, &r) == DBW_OK);
    assert(r.new_album == 1 && r.new_artist == 1 && r.tracks_after == 7);
    expect_ok(db2);
    assert(database_load(&d, db2) == DATABASE_OK && d.count == 7);
    assert(strcmp(d.tracks[6].title, "Gr\xC3\xBC\xC3\x9F" " Gott \xF0\x9F\x8E\xB5") == 0);
    assert(strcmp(d.tracks[6].artist, "New \xC3\x89" "mile") == 0);

    // --- blanks become placeholders; a second track by the same new artist reuses its album ---
    dbw_track_t t3 = base_track("Nameless", NULL, "", ":iPod_Control:Music:F00:XXXX.mp3");
    assert(add(db2, db3, &t3, &r) == DBW_OK && r.new_album == 1);
    expect_ok(db3);
    assert(database_load(&d, db3) == DATABASE_OK);
    assert(strcmp(d.tracks[7].artist, "Unknown Artist") == 0 && strcmp(d.tracks[7].album, "Unknown Album") == 0);
    dbw_track_t t4 = base_track("Another", NULL, NULL, ":iPod_Control:Music:F00:WWWW.mp3");
    char db4[64]; snprintf(db4, sizeof db4, "/tmp/dbw_%ld_4.db", (long)getpid());
    assert(add(db3, db4, &t4, &r) == DBW_OK && r.new_album == 0 && r.new_artist == 0);
    expect_ok(db4);
    remove(db4);

    // --- the input is never modified, and output must differ from input ---
    {
        FILE *f = fopen(db0, "rb"); unsigned char *back = malloc(sizeof fixture_db);
        assert(fread(back, 1, sizeof fixture_db, f) == sizeof fixture_db); fclose(f);
        assert(memcmp(back, fixture_db, sizeof fixture_db) == 0);
        free(back);
    }
    assert(dbwriter_add_track(db0, db0, &t1, &r) == DBW_ERR_ARG);

    // --- unusable details are refused ---
    dbw_track_t bt = t1; bt.title = "";                         assert(add(db0, bad, &bt, &r) == DBW_ERR_ARG);
    bt = t1; bt.ipod_path = "/etc/passwd";                      assert(add(db0, bad, &bt, &r) == DBW_ERR_ARG);
    bt = t1; bt.ipod_path = NULL;                               assert(add(db0, bad, &bt, &r) == DBW_ERR_ARG);
    bt = t1; bt.sample_rate = 0;                                assert(add(db0, bad, &bt, &r) == DBW_ERR_ARG);
    bt = t1; bt.length_ms = 0;                                  assert(add(db0, bad, &bt, &r) == DBW_ERR_ARG);
    assert(dbwriter_add_track(NULL, bad, &t1, &r) == DBW_ERR_NULL);
    assert(dbwriter_add_track(db0, bad, NULL, &r) == DBW_ERR_NULL);
    assert(add("/no/such/file", bad, &t1, &r) == DBW_ERR_IO);
    assert(access(bad, F_OK) != 0);                             // nothing was left behind

    // --- databases it doesn't understand are refused, and nothing is written ---
    {
        unsigned char *copy = malloc(sizeof fixture_db);
        memcpy(copy, fixture_db, sizeof fixture_db);
        copy[4] = 200;                                           // wrong header size
        write_file(bad, copy, sizeof fixture_db);
        char out[100]; snprintf(out, sizeof out, "%s.out", bad);
        assert(add(bad, out, &t1, &r) == DBW_ERR_UNSUPPORTED);
        assert(access(out, F_OK) != 0);

        memcpy(copy, fixture_db, sizeof fixture_db);
        write_file(bad, copy, sizeof fixture_db - 100);          // truncated
        assert(add(bad, out, &t1, &r) == DBW_ERR_FORMAT);
        assert(access(out, F_OK) != 0);
        free(copy);

        write_file(bad, (const unsigned char *)"not a database at all", 21);
        assert(add(bad, out, &t1, &r) == DBW_ERR_FORMAT);
    }

    // --- damaged databases must be refused or accepted safely, never crash ---
    {
        srand(99);
        char out[100]; snprintf(out, sizeof out, "%s.out", bad);
        unsigned char *copy = malloc(sizeof fixture_db);
        for (int i = 0; i < 1500; i++) {
            memcpy(copy, fixture_db, sizeof fixture_db);
            size_t n = sizeof fixture_db;
            for (int k = 0; k < 1 + rand() % 4; k++) copy[rand() % n] = (unsigned char)rand();
            if (rand() % 4 == 0) n = (size_t)rand() % n;
            write_file(bad, copy, n);
            int rc = add(bad, out, &t1, &r);
            if (rc == DBW_OK) expect_ok(out);                    // if it was accepted, the result must still be sound
            else assert(rc == DBW_ERR_FORMAT || rc == DBW_ERR_UNSUPPORTED);
            int vr = dbwriter_verify(bad, NULL, 0);
            assert(vr == DBW_OK || vr == DBW_ERR_FORMAT || vr == DBW_ERR_UNSUPPORTED);
        }
        free(copy);
        remove(out);
    }

    // --- optional: run against a real database copy: REAL_DB=/path/to/iTunesDB ./test_dbwriter ---
    const char *real = getenv("REAL_DB");
    if (real && real[0]) {
        char err[256];
        int rc = dbwriter_verify(real, err, sizeof err);
        printf("real database check: %s\n", rc == DBW_OK ? "passes every structure check" : err);
        assert(rc == DBW_OK);
        dbw_track_t rt = base_track("Real DB Test Song", "Some Artist", "Some Album", ":iPod_Control:Music:F00:TEST.mp3");
        assert(add(real, db1, &rt, &r) == DBW_OK);
        expect_ok(db1);
        printf("real database + 1 track: %zu -> %zu tracks, checks pass\n", r.tracks_before, r.tracks_after);
    }

    database_free(&d);
    remove(db0); remove(db1); remove(db2); remove(db3); remove(bad);
    { char o[100]; snprintf(o, sizeof o, "%s.out", bad); remove(o); }
    puts("dbwriter tests passed");
    return 0;
}
