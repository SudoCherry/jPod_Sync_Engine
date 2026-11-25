#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database.h"
#include "textconv.h"

#define DB_FILE_MAX (64u * 1024u * 1024u)   // refuse absurdly large files

// ---------- safe little-endian reading ----------
// Every read is bounds-checked: a damaged file must never make us read
// outside the buffer, because we cannot trust any size stored in it.

static int get32(const uint8_t *b, size_t len, size_t off, uint32_t *out) {
    if (off > len || len - off < 4) return -1;
    *out = (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
           ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
    return 0;
}

static int has_tag(const uint8_t *b, size_t len, size_t off, const char *tag) {
    return off <= len && len - off >= 4 && memcmp(b + off, tag, 4) == 0;
}

// ---------- the track list ----------

int database_init(database_t *db) {
    if (db == NULL) return DATABASE_ERR_NULL;
    db->tracks = NULL;
    db->count = 0;
    db->capacity = 0;
    return DATABASE_OK;
}

void database_free(database_t *db) {
    if (db == NULL) return;
    free(db->tracks);
    db->tracks = NULL;
    db->count = 0;
    db->capacity = 0;
}

static int database_add(database_t *db, const db_track_t *t) {
    if (db->count == db->capacity) {
        size_t new_cap = db->capacity == 0 ? 64 : db->capacity * 2;
        db_track_t *bigger = realloc(db->tracks, new_cap * sizeof(db_track_t));
        if (bigger == NULL) return DATABASE_ERR_MEMORY;
        db->tracks = bigger;
        db->capacity = new_cap;
    }
    db->tracks[db->count++] = *t;
    return DATABASE_OK;
}

// ---------- parsing the tree: mhbd > mhsd > mhlt > mhit > mhod ----------

// mhod types we care about
enum { MHOD_TITLE = 1, MHOD_PATH = 2, MHOD_ALBUM = 3, MHOD_ARTIST = 4 };

// Parse one mhit (a track) starting at `off`. Sets *next to the following chunk.
static int parse_mhit(database_t *db, const uint8_t *b, size_t len, size_t off, size_t *next) {
    uint32_t hdr, total, nmhod, id;

    if (!has_tag(b, len, off, "mhit")) return DATABASE_ERR_FORMAT;
    if (get32(b, len, off + 4, &hdr) || get32(b, len, off + 8, &total) ||
        get32(b, len, off + 12, &nmhod) || get32(b, len, off + 16, &id)) {
        return DATABASE_ERR_FORMAT;
    }
    if (hdr < 20 || total < hdr || total > len - off) return DATABASE_ERR_FORMAT;

    db_track_t t;
    memset(&t, 0, sizeof(t));
    t.id = id;

    size_t pos = off + hdr;
    size_t end = off + total;

    for (uint32_t i = 0; i < nmhod; i++) {
        uint32_t mtotal, mtype, xl;

        if (end - pos < 24 || !has_tag(b, len, pos, "mhod")) return DATABASE_ERR_FORMAT;
        if (get32(b, len, pos + 8, &mtotal) || get32(b, len, pos + 12, &mtype)) {
            return DATABASE_ERR_FORMAT;
        }
        if (mtotal < 24 || mtotal > end - pos) return DATABASE_ERR_FORMAT;

        // string mhods: byte length at +28, UTF-16LE text from +40
        if (mtype >= MHOD_TITLE && mtype <= MHOD_ARTIST && mtotal >= 40 &&
            get32(b, len, pos + 28, &xl) == 0 && xl <= mtotal - 40) {

            char *dest = NULL;
            switch (mtype) {
                case MHOD_TITLE:  dest = t.title;  break;
                case MHOD_PATH:   dest = t.path;   break;
                case MHOD_ALBUM:  dest = t.album;  break;
                case MHOD_ARTIST: dest = t.artist; break;
            }
            utf16_to_utf8(b + pos + 40, xl, 0, dest, DB_STR_MAX);   // 0 = little-endian
        }
        pos += mtotal;
    }

    // the iPod stores paths with ':' separators, e.g. ":iPod_Control:Music:F00:ABCD.mp3"
    for (char *c = t.path; *c; c++) {
        if (*c == ':') *c = '/';
    }

    *next = end;
    return database_add(db, &t);
}

static int parse_database(database_t *db, const uint8_t *b, size_t len) {
    uint32_t hdr;

    if (!has_tag(b, len, 0, "mhbd") || get32(b, len, 4, &hdr)) return DATABASE_ERR_FORMAT;
    if (hdr < 12 || hdr > len) return DATABASE_ERR_FORMAT;

    size_t off = hdr;
    while (off < len) {
        uint32_t sd_hdr, sd_total, sd_type;

        if (!has_tag(b, len, off, "mhsd")) return DATABASE_ERR_FORMAT;
        if (get32(b, len, off + 4, &sd_hdr) || get32(b, len, off + 8, &sd_total) ||
            get32(b, len, off + 12, &sd_type)) {
            return DATABASE_ERR_FORMAT;
        }
        if (sd_hdr < 16 || sd_total < sd_hdr || sd_total > len - off) return DATABASE_ERR_FORMAT;

        if (sd_type == 1) {                       // 1 = track list (2 = playlists, skipped)
            size_t lt = off + sd_hdr;
            uint32_t lt_hdr, count;

            if (!has_tag(b, len, lt, "mhlt") || get32(b, len, lt + 4, &lt_hdr) ||
                get32(b, len, lt + 8, &count)) {
                return DATABASE_ERR_FORMAT;
            }
            if (lt_hdr < 12 || lt_hdr > len - lt) return DATABASE_ERR_FORMAT;

            size_t moff = lt + lt_hdr;
            for (uint32_t i = 0; i < count; i++) {
                int rc = parse_mhit(db, b, len, moff, &moff);
                if (rc != DATABASE_OK) return rc;
            }
        }
        off += sd_total;
    }
    return DATABASE_OK;
}

// Read a whole file into memory. The caller frees *buf_out.
static int read_whole_file(const char *path, uint8_t **buf_out, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return DATABASE_ERR_IO;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return DATABASE_ERR_IO; }
    long size = ftell(f);
    if (size < 0 || (unsigned long)size > DB_FILE_MAX) { fclose(f); return DATABASE_ERR_FORMAT; }
    rewind(f);

    uint8_t *buf = malloc(size > 0 ? (size_t)size : 1);
    if (buf == NULL) { fclose(f); return DATABASE_ERR_MEMORY; }

    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return DATABASE_ERR_IO; }

    *buf_out = buf;
    *len_out = (size_t)size;
    return DATABASE_OK;
}

int database_load(database_t *db, const char *itunesdb_path) {
    if (db == NULL || itunesdb_path == NULL) return DATABASE_ERR_NULL;

    uint8_t *buf;
    size_t len;
    int rc = read_whole_file(itunesdb_path, &buf, &len);
    if (rc != DATABASE_OK) return rc;

    database_free(db);                       // replace anything already loaded
    rc = parse_database(db, buf, len);
    if (rc != DATABASE_OK) database_free(db);   // never hand back a half-parsed list

    free(buf);
    return rc;
}

// ---------- inspector: print the file's structure (read-only) ----------

#define TYPE_BUCKETS 128

static void print_tag(FILE *out, const uint8_t *p) {
    for (int i = 0; i < 4; i++) fputc((p[i] >= 32 && p[i] < 127) ? p[i] : '?', out);
}

static void hex_dump(FILE *out, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i += 16) {
        fprintf(out, "    +%03zu:", i);
        for (size_t j = i; j < i + 16 && j < n; j++) fprintf(out, " %02x", b[j]);
        fputc('\n', out);
    }
}

static void inspect_tracks(const uint8_t *b, size_t len, size_t lt, FILE *out) {
    uint32_t lt_hdr, count;
    if (get32(b, len, lt + 4, &lt_hdr) || get32(b, len, lt + 8, &count) ||
        lt_hdr < 12 || lt_hdr > len - lt) {
        fprintf(out, "  (track list header unreadable)\n");
        return;
    }

    uint32_t min_hdr = 0xFFFFFFFFu, max_hdr = 0, min_m = 0xFFFFFFFFu, max_m = 0;
    uint32_t types[TYPE_BUCKETS] = {0}, other = 0, seen = 0;
    size_t moff = lt + lt_hdr;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t hdr, total, nmhod;
        if (!has_tag(b, len, moff, "mhit") || get32(b, len, moff + 4, &hdr) ||
            get32(b, len, moff + 8, &total) || get32(b, len, moff + 12, &nmhod) ||
            hdr < 20 || total < hdr || total > len - moff) {
            fprintf(out, "  (stopped at track %u: unreadable)\n", i);
            break;
        }
        if (hdr < min_hdr) min_hdr = hdr;
        if (hdr > max_hdr) max_hdr = hdr;
        if (nmhod < min_m) min_m = nmhod;
        if (nmhod > max_m) max_m = nmhod;

        if (i == 0) {
            fprintf(out, "  first track: mhit header=%u bytes, total=%u, mhods=%u\n", hdr, total, nmhod);
            fprintf(out, "  first track header bytes:\n");
            hex_dump(out, b + moff, hdr > 512 ? 512 : hdr);
        }

        size_t pos = moff + hdr, end = moff + total;
        for (uint32_t k = 0; k < nmhod; k++) {
            uint32_t mtotal, mtype;
            if (end - pos < 24 || !has_tag(b, len, pos, "mhod") ||
                get32(b, len, pos + 8, &mtotal) || get32(b, len, pos + 12, &mtype) ||
                mtotal < 24 || mtotal > end - pos) break;
            if (mtype < TYPE_BUCKETS) types[mtype]++; else other++;
            if (i == 0) fprintf(out, "    mhod type=%u total=%u\n", mtype, mtotal);
            pos += mtotal;
        }
        moff += total;
        seen++;
    }

    fprintf(out, "  tracks read: %u of %u\n", seen, count);
    if (seen > 0) {
        fprintf(out, "  mhit header size: %u..%u bytes, mhods per track: %u..%u\n",
                min_hdr, max_hdr, min_m, max_m);
        fprintf(out, "  mhod types across all tracks:");
        for (int t = 0; t < TYPE_BUCKETS; t++) {
            if (types[t]) fprintf(out, " [type %d: %u]", t, types[t]);
        }
        if (other) fprintf(out, " [other: %u]", other);
        fputc('\n', out);
    }
}

// Print a chunk's tag, sizes and the start of its header bytes
static void peek_chunk(FILE *out, const char *label, const uint8_t *b, size_t len, size_t off, size_t cap) {
    uint32_t hdr, total;
    if (off > len || len - off < 12 || get32(b, len, off + 4, &hdr) || get32(b, len, off + 8, &total)) {
        fprintf(out, "  %s: unreadable\n", label);
        return;
    }
    fprintf(out, "  %s: ", label);
    print_tag(out, b + off);
    fprintf(out, " header=%u total=%u\n", hdr, total);
    size_t n = hdr;
    if (n > cap) n = cap;
    if (n > len - off) n = len - off;
    hex_dump(out, b + off, n);
}

// For the album and playlist sections: show the first entry of each kind
static void inspect_other(const uint8_t *b, size_t len, size_t ch, FILE *out) {
    uint32_t ch_hdr, count;
    if (!(has_tag(b, len, ch, "mhla") || has_tag(b, len, ch, "mhlp"))) return;
    if (get32(b, len, ch + 4, &ch_hdr) || get32(b, len, ch + 8, &count)) return;
    if (count == 0 || ch_hdr < 12 || ch_hdr > len - ch) return;

    size_t first = ch + ch_hdr;
    if (has_tag(b, len, first, "mhia")) {
        peek_chunk(out, "first album item", b, len, first, 160);
        return;
    }
    if (!has_tag(b, len, first, "mhyp")) return;
    peek_chunk(out, "first playlist", b, len, first, 240);

    uint32_t yhdr, nmhod, nmhip;
    if (get32(b, len, first + 4, &yhdr) || get32(b, len, first + 12, &nmhod) ||
        get32(b, len, first + 16, &nmhip) || nmhip == 0 || yhdr < 20 || yhdr > len - first) return;

    size_t pos = first + yhdr;                       // skip the playlist's own mhods
    for (uint32_t k = 0; k < nmhod; k++) {
        uint32_t mt;
        if (!has_tag(b, len, pos, "mhod") || get32(b, len, pos + 8, &mt) || mt < 24 || mt > len - pos) return;
        pos += mt;
    }
    if (has_tag(b, len, pos, "mhip")) peek_chunk(out, "first playlist item (mhip)", b, len, pos, 128);
}

static int inspect_buffer(const uint8_t *b, size_t len, FILE *out) {
    uint32_t hdr, total, w12 = 0, w16 = 0, w20 = 0, w24 = 0;

    if (!has_tag(b, len, 0, "mhbd") || get32(b, len, 4, &hdr) || get32(b, len, 8, &total)) {
        return DATABASE_ERR_FORMAT;
    }
    get32(b, len, 12, &w12); get32(b, len, 16, &w16);
    get32(b, len, 20, &w20); get32(b, len, 24, &w24);

    fprintf(out, "File size: %zu bytes\n", len);
    fprintf(out, "mhbd: header=%u total=%u, words at +12/+16/+20/+24 = 0x%x 0x%x 0x%x 0x%x\n",
            hdr, total, w12, w16, w20, w24);
    if (hdr < 12 || hdr > len) return DATABASE_ERR_FORMAT;
    fprintf(out, "mhbd header bytes:\n");
    hex_dump(out, b, hdr > 512 ? 512 : hdr);

    size_t off = hdr;
    for (int idx = 0; off < len; idx++) {
        uint32_t sd_hdr, sd_total, sd_type;
        if (!has_tag(b, len, off, "mhsd") || get32(b, len, off + 4, &sd_hdr) ||
            get32(b, len, off + 8, &sd_total) || get32(b, len, off + 12, &sd_type) ||
            sd_hdr < 16 || sd_total < sd_hdr || sd_total > len - off) {
            return DATABASE_ERR_FORMAT;
        }

        fprintf(out, "mhsd #%d: type=%u header=%u total=%u", idx, sd_type, sd_hdr, sd_total);
        size_t ch = off + sd_hdr;
        uint32_t ch_hdr, ch_count;
        if (has_tag(b, len, ch, "mhlt") || has_tag(b, len, ch, "mhlp") || has_tag(b, len, ch, "mhla")) {
            if (get32(b, len, ch + 4, &ch_hdr) == 0 && get32(b, len, ch + 8, &ch_count) == 0) {
                fprintf(out, "  child=");
                print_tag(out, b + ch);
                fprintf(out, " header=%u count=%u", ch_hdr, ch_count);
            }
        }
        fputc('\n', out);

        if (sd_type == 1 && has_tag(b, len, ch, "mhlt")) inspect_tracks(b, len, ch, out);
        if (sd_type != 1) inspect_other(b, len, ch, out);
        off += sd_total;
    }
    return DATABASE_OK;
}

int database_inspect(const char *itunesdb_path, FILE *out) {
    if (itunesdb_path == NULL || out == NULL) return DATABASE_ERR_NULL;

    uint8_t *buf;
    size_t len;
    int rc = read_whole_file(itunesdb_path, &buf, &len);
    if (rc != DATABASE_OK) return rc;

    rc = inspect_buffer(buf, len, out);
    free(buf);
    return rc;
}
