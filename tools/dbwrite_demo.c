// Command-line tool for the database writer. It never touches an iPod by itself:
// it reads one iTunesDB file and writes a NEW file.
//
//   ./dbwrite_demo verify <iTunesDB>
//   ./dbwrite_demo add <in_iTunesDB> <out_iTunesDB> <song.mp3> <ipod_path>
//
// <ipod_path> is the database-style path, e.g. :iPod_Control:Music:F07:ABCD.mp3

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "dbwriter.h"
#include "database.h"
#include "mp3info.h"
#include "tags.h"

static int verify(const char *path) {
    char err[256];
    int rc = dbwriter_verify(path, err, sizeof(err));
    if (rc == DBW_OK) {
        printf("OK: %s passes every structure check.\n", path);
        return 0;
    }
    printf("NOT OK (error %d): %s\n", rc, err);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "verify") == 0) return verify(argv[2]);

    if (argc != 6 || strcmp(argv[1], "add") != 0) {
        printf("Usage:\n  %s verify <iTunesDB>\n  %s add <in_iTunesDB> <out_iTunesDB> <song.mp3> <ipod_path>\n", argv[0], argv[0]);
        return 2;
    }
    const char *in_db = argv[2], *out_db = argv[3], *mp3 = argv[4], *ipod_path = argv[5];

    mp3info_t mi;
    int rc = mp3info_read(mp3, &mi);
    if (rc != MP3INFO_OK) { printf("Can't read that MP3 (error %d)\n", rc); return 1; }

    tags_t tg, guess;
    if (tags_read(mp3, &tg) != TAGS_OK) memset(&tg, 0, sizeof(tg));
    tags_from_filename(mp3, &guess);
    if (tg.title[0] == '\0') snprintf(tg.title, sizeof(tg.title), "%s", guess.title);
    if (tg.artist[0] == '\0' && tg.album[0] == '\0') snprintf(tg.artist, sizeof(tg.artist), "%s", guess.artist);

    struct stat st;
    time_t mtime = (stat(mp3, &st) == 0) ? st.st_mtime : time(NULL);

    dbw_track_t t = {
        .title = tg.title, .artist = tg.artist, .album = tg.album, .album_artist = NULL,
        .ipod_path = ipod_path,
        .size_bytes = mi.size_bytes, .length_ms = mi.length_ms, .bitrate_kbps = mi.bitrate_kbps,
        .sample_rate = mi.sample_rate, .vbr = mi.vbr,
        .added_time = dbwriter_mac_time(time(NULL)), .modified_time = dbwriter_mac_time(mtime),
    };

    printf("Adding: \"%s\" by \"%s\" [%s], %u:%02u, %u kbps%s, %u Hz\n", t.title, t.artist, t.album,
           mi.length_ms / 60000, (mi.length_ms / 1000) % 60, mi.bitrate_kbps, mi.vbr ? " VBR" : "", mi.sample_rate);

    dbw_report_t rep;
    rc = dbwriter_add_track(in_db, out_db, &t, &rep);
    if (rc != DBW_OK) { printf("Not written (error %d): %s\n", rc, rep.message); return 1; }

    printf("Wrote %s\n", out_db);
    printf("  tracks: %zu -> %zu, file size: %zu -> %zu bytes\n", rep.tracks_before, rep.tracks_after, rep.bytes_before, rep.bytes_after);
    printf("  new track id %u, album id %u (%s)\n", rep.track_id, rep.album_id, rep.new_album ? "new album" : "existing album");

    // re-read the result with the independent reader as a second opinion
    database_t db;
    database_init(&db);
    if (database_load(&db, out_db) == DATABASE_OK) {
        const db_track_t *last = &db.tracks[db.count - 1];
        printf("  reader sees %zu tracks; last one: [%u] %s - %s  (%s)\n", db.count, last->id, last->artist, last->title, last->path);
    } else {
        printf("  WARNING: the reader could not load the result\n");
    }
    database_free(&db);
    return verify(out_db);
}
