#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "dbwriter.h"
#include "textconv.h"

// ---------------------------------------------------------------------------
// The one layout this writer understands (what iOpenPod writes for click-wheel iPods).
// Anything else is refused rather than guessed at.
// ---------------------------------------------------------------------------
enum {
    MHBD_HDR = 244, MHSD_HDR = 96, MHL_HDR = 92, MHIA_HDR = 88, MHIT_HDR = 624,
    MHYP_HDR = 184, MHIP_HDR = 76, MHIP_SIZE = 120, MHOD52_BODY = 72, MAX_SORTS = 16
};
#define PATH_PREFIX ":iPod_Control:Music:"

// ---------- little-endian helpers ----------
static uint32_t rd32(const uint8_t *b, size_t o) {
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
static uint64_t rd64(const uint8_t *b, size_t o) { return (uint64_t)rd32(b, o) | ((uint64_t)rd32(b, o + 4) << 32); }
static void wr32(uint8_t *b, size_t o, uint32_t v) {
    b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *b, size_t o, uint64_t v) { wr32(b, o, (uint32_t)v); wr32(b, o + 4, (uint32_t)(v >> 32)); }

// ---------- growable byte buffer ----------
typedef struct { uint8_t *d; size_t n, cap; } bytes_t;

static int bt_reserve(bytes_t *t, size_t extra) {
    if (t->n + extra <= t->cap) return 0;
    size_t cap = t->cap ? t->cap : 256;
    while (cap < t->n + extra) cap *= 2;
    uint8_t *nd = realloc(t->d, cap);
    if (nd == NULL) return -1;
    t->d = nd; t->cap = cap;
    return 0;
}
static int bt_put(bytes_t *t, const void *src, size_t n) {
    if (bt_reserve(t, n)) return -1;
    if (n) memcpy(t->d + t->n, src, n);
    t->n += n;
    return 0;
}
static int bt_put32(bytes_t *t, uint32_t v) { uint8_t x[4]; wr32(x, 0, v); return bt_put(t, x, 4); }
static int bt_insert(bytes_t *t, size_t pos, const void *src, size_t n) {
    if (pos > t->n || bt_reserve(t, n)) return -1;
    memmove(t->d + pos + n, t->d + pos, t->n - pos);
    memcpy(t->d + pos, src, n);
    t->n += n;
    return 0;
}

// ---------------------------------------------------------------------------
// Structure check. Also records where everything is, for the writer.
// ---------------------------------------------------------------------------
typedef struct {
    size_t sec;              // offset of this playlist's mhsd
    size_t mhyp;             // offset of its mhyp
    size_t mhyp_total;
    size_t first_mhip;
    size_t n_sorts;
    size_t sort_off[MAX_SORTS];
    uint32_t sort_type[MAX_SORTS];
} pl_info_t;

typedef struct {
    size_t n_tracks, n_albums;
    size_t *track_off, *album_off;
    size_t sec_albums, sec_tracks;
    int have_albums, have_tracks;
    pl_info_t pl[2];
    int n_pl;
    uint32_t max_id;
} parsed_t;

static void parsed_free(parsed_t *P) {
    free(P->track_off); free(P->album_off);
    memset(P, 0, sizeof(*P));
}

#define FAIL(code, ...) do { snprintf(err, errsz, __VA_ARGS__); rc = (code); goto done; } while (0)
#define NEED(cond, code, ...) do { if (!(cond)) FAIL(code, __VA_ARGS__); } while (0)

static int is_string_mhod(uint32_t t) {
    return t == 1 || t == 2 || t == 3 || t == 4 || t == 5 || t == 6 || t == 8 || t == 12 || t == 22 ||
           t == 23 || (t >= 27 && t <= 31) || t == 200 || t == 201 || t == 202;
}

static void bump(parsed_t *P, uint32_t id) { if (id > P->max_id) P->max_id = id; }

// Check a run of `count` mhods starting at `pos`, which must end exactly at `end`.
// For string mhods also check that the text fits.
static int check_mhods(const uint8_t *b, size_t pos, size_t end, uint32_t count, char *err, size_t errsz, const char *what) {
    for (uint32_t k = 0; k < count; k++) {
        if (end - pos < 24 || memcmp(b + pos, "mhod", 4) != 0) { snprintf(err, errsz, "%s: bad chunk %u", what, k); return DBW_ERR_FORMAT; }
        uint32_t mtot = rd32(b, pos + 8), mtype = rd32(b, pos + 12);
        if (mtot < 24 || mtot > end - pos) { snprintf(err, errsz, "%s: chunk %u has a bad size", what, k); return DBW_ERR_FORMAT; }
        if (is_string_mhod(mtype)) {
            if (mtot < 40 || rd32(b, pos + 28) > mtot - 40) { snprintf(err, errsz, "%s: text chunk %u does not fit", what, k); return DBW_ERR_FORMAT; }
        }
        pos += mtot;
    }
    if (pos != end) { snprintf(err, errsz, "%s: chunks do not add up to the record size", what); return DBW_ERR_FORMAT; }
    return DBW_OK;
}

static int verify_buffer(const uint8_t *b, size_t len, char *err, size_t errsz, parsed_t *P) {
    int rc = DBW_OK;
    uint8_t *seen = NULL;
    memset(P, 0, sizeof(*P));
    err[0] = '\0';

    NEED(len >= MHBD_HDR && memcmp(b, "mhbd", 4) == 0, DBW_ERR_FORMAT, "not an iTunesDB (no mhbd header)");
    NEED(rd32(b, 4) == MHBD_HDR, DBW_ERR_UNSUPPORTED, "database header is %u bytes; this writer only knows %d", rd32(b, 4), MHBD_HDR);
    NEED(rd32(b, 8) == len, DBW_ERR_FORMAT, "header says %u bytes but the file has %zu", rd32(b, 8), len);

    // ---- sections ----
    uint32_t nsec = rd32(b, 20), nseen = 0;
    size_t sec_off[8], sec_tot[8]; uint32_t sec_type[8];
    size_t off = MHBD_HDR;
    while (off < len) {
        NEED(nseen < 8, DBW_ERR_UNSUPPORTED, "too many sections");
        NEED(len - off >= MHSD_HDR && memcmp(b + off, "mhsd", 4) == 0, DBW_ERR_FORMAT, "section %u: not an mhsd", nseen);
        uint32_t hdr = rd32(b, off + 4), tot = rd32(b, off + 8), type = rd32(b, off + 12);
        NEED(hdr == MHSD_HDR, DBW_ERR_UNSUPPORTED, "section header is %u bytes, expected %d", hdr, MHSD_HDR);
        NEED(tot >= hdr + MHL_HDR && tot <= len - off, DBW_ERR_FORMAT, "section %u has a bad size", nseen);
        NEED(type == 1 || type == 2 || type == 3 || type == 4 || type == 5, DBW_ERR_UNSUPPORTED, "unknown section type %u", type);
        sec_off[nseen] = off; sec_tot[nseen] = tot; sec_type[nseen] = type;
        off += tot; nseen++;
    }
    NEED(nseen == nsec, DBW_ERR_FORMAT, "header lists %u sections, found %u", nsec, nseen);

    for (uint32_t s = 0; s < nseen; s++) {
        if (sec_type[s] == 4) { NEED(!P->have_albums, DBW_ERR_UNSUPPORTED, "two album sections"); P->have_albums = 1; P->sec_albums = sec_off[s]; }
        if (sec_type[s] == 1) { NEED(!P->have_tracks, DBW_ERR_UNSUPPORTED, "two track sections"); P->have_tracks = 1; P->sec_tracks = sec_off[s]; }
        if (sec_type[s] == 2 || sec_type[s] == 3) {
            NEED(P->n_pl < 2, DBW_ERR_UNSUPPORTED, "too many playlist sections");
            P->pl[P->n_pl++].sec = sec_off[s];
        }
    }
    NEED(P->have_albums && P->have_tracks && P->n_pl == 2, DBW_ERR_UNSUPPORTED,
         "expected album, track and two playlist sections");

    // ---- albums ----
    for (uint32_t s = 0; s < nseen; s++) {
        if (sec_type[s] != 4) continue;
        size_t lo = sec_off[s] + MHSD_HDR, end = sec_off[s] + sec_tot[s];
        NEED(memcmp(b + lo, "mhla", 4) == 0 && rd32(b, lo + 4) == MHL_HDR, DBW_ERR_UNSUPPORTED, "album list header not recognised");
        uint32_t count = rd32(b, lo + 8);
        NEED(count <= (end - lo) / MHIA_HDR, DBW_ERR_FORMAT, "album count %u is impossible", count);
        P->album_off = malloc((count ? count : 1) * sizeof(size_t));
        NEED(P->album_off != NULL, DBW_ERR_MEMORY, "out of memory");
        size_t pos = lo + MHL_HDR;
        uint32_t prev = 0;
        for (uint32_t i = 0; i < count; i++) {
            NEED(end - pos >= MHIA_HDR && memcmp(b + pos, "mhia", 4) == 0, DBW_ERR_FORMAT, "album %u: bad chunk", i);
            uint32_t hdr = rd32(b, pos + 4), tot = rd32(b, pos + 8), nm = rd32(b, pos + 12), id = rd32(b, pos + 16);
            NEED(hdr == MHIA_HDR, DBW_ERR_UNSUPPORTED, "album header is %u bytes, expected %d", hdr, MHIA_HDR);
            NEED(tot >= hdr && tot <= end - pos, DBW_ERR_FORMAT, "album %u: bad size", i);
            NEED(nm >= 2 && nm <= 4, DBW_ERR_UNSUPPORTED, "album %u has %u text chunks", i, nm);
            NEED(i == 0 || id > prev, DBW_ERR_UNSUPPORTED, "album ids are not ascending");
            int rc2 = check_mhods(b, pos + hdr, pos + tot, nm, err, errsz, "album");
            if (rc2) { rc = rc2; goto done; }
            NEED(rd32(b, pos + hdr + 12) == 200 && rd32(b, pos + hdr + rd32(b, pos + hdr + 8) + 12) == 201,
                 DBW_ERR_UNSUPPORTED, "album %u: unexpected text chunk order", i);
            P->album_off[i] = pos; prev = id; bump(P, id);
            pos += tot;
        }
        NEED(pos == end, DBW_ERR_FORMAT, "album section has leftover bytes");
        P->n_albums = count;
    }

    // ---- tracks ----
    for (uint32_t s = 0; s < nseen; s++) {
        if (sec_type[s] != 1) continue;
        size_t lo = sec_off[s] + MHSD_HDR, end = sec_off[s] + sec_tot[s];
        NEED(memcmp(b + lo, "mhlt", 4) == 0 && rd32(b, lo + 4) == MHL_HDR, DBW_ERR_UNSUPPORTED, "track list header not recognised");
        uint32_t count = rd32(b, lo + 8);
        NEED(count <= (end - lo) / MHIT_HDR, DBW_ERR_FORMAT, "track count %u is impossible", count);
        P->track_off = malloc((count ? count : 1) * sizeof(size_t));
        NEED(P->track_off != NULL, DBW_ERR_MEMORY, "out of memory");
        size_t pos = lo + MHL_HDR;
        uint32_t prev = 0;
        for (uint32_t i = 0; i < count; i++) {
            NEED(end - pos >= MHIT_HDR && memcmp(b + pos, "mhit", 4) == 0, DBW_ERR_FORMAT, "track %u: bad chunk", i);
            uint32_t hdr = rd32(b, pos + 4), tot = rd32(b, pos + 8), nm = rd32(b, pos + 12), id = rd32(b, pos + 16);
            NEED(hdr == MHIT_HDR, DBW_ERR_UNSUPPORTED, "track %u: header is %u bytes, this writer only knows %d", i, hdr, MHIT_HDR);
            NEED(tot >= hdr && tot <= end - pos, DBW_ERR_FORMAT, "track %u: bad size", i);
            NEED(i == 0 || id > prev, DBW_ERR_UNSUPPORTED, "track ids are not ascending at track %u", i);
            NEED(rd32(b, pos + 500) == id + 1, DBW_ERR_UNSUPPORTED, "track %u: unexpected second id", i);
            NEED(rd32(b, pos + 24) == 0x4d503320u, DBW_ERR_UNSUPPORTED, "track %u is not an MP3; only MP3 libraries are supported", i);
            int rc2 = check_mhods(b, pos + hdr, pos + tot, nm, err, errsz, "track");
            if (rc2) { rc = rc2; goto done; }
            // its album must exist (album ids are ascending, so binary search)
            uint32_t aid = rd32(b, pos + 288);
            size_t lo2 = 0, hi2 = P->n_albums; int found = 0;
            while (lo2 < hi2) {
                size_t mid = (lo2 + hi2) / 2; uint32_t v = rd32(b, P->album_off[mid] + 16);
                if (v == aid) { found = 1; break; }
                if (v < aid) lo2 = mid + 1; else hi2 = mid;
            }
            NEED(found, DBW_ERR_FORMAT, "track %u points at album %u, which doesn't exist", i, aid);
            P->track_off[i] = pos; prev = id; bump(P, id); bump(P, id + 1); bump(P, rd32(b, pos + 480));
            pos += tot;
        }
        NEED(pos == end, DBW_ERR_FORMAT, "track section has leftover bytes");
        P->n_tracks = count;
    }

    // ---- playlists (both copies) ----
    seen = malloc(P->n_tracks ? P->n_tracks : 1);
    NEED(seen != NULL, DBW_ERR_MEMORY, "out of memory");
    int plslot = 0;
    for (uint32_t s = 0; s < nseen; s++) {
        if (sec_type[s] != 2 && sec_type[s] != 3) continue;
        pl_info_t *pl = &P->pl[plslot++];
        size_t lo = sec_off[s] + MHSD_HDR, end = sec_off[s] + sec_tot[s];
        NEED(memcmp(b + lo, "mhlp", 4) == 0 && rd32(b, lo + 4) == MHL_HDR && rd32(b, lo + 8) == 1,
             DBW_ERR_UNSUPPORTED, "playlist section: expected exactly one playlist");
        size_t yp = lo + MHL_HDR;
        NEED(end - yp >= MHYP_HDR && memcmp(b + yp, "mhyp", 4) == 0 && rd32(b, yp + 4) == MHYP_HDR,
             DBW_ERR_UNSUPPORTED, "playlist header not recognised");
        uint32_t ytot = rd32(b, yp + 8), nm = rd32(b, yp + 12), nip = rd32(b, yp + 16);
        NEED(ytot >= MHYP_HDR && ytot == end - yp, DBW_ERR_FORMAT, "playlist size does not match its section");
        NEED(nip == P->n_tracks, DBW_ERR_UNSUPPORTED, "playlist holds %u items but there are %zu tracks", nip, P->n_tracks);
        pl->mhyp = yp; pl->mhyp_total = ytot; pl->sec = sec_off[s];

        size_t pos = yp + MHYP_HDR;
        for (uint32_t k = 0; k < nm; k++) {
            NEED(end - pos >= 24 && memcmp(b + pos, "mhod", 4) == 0, DBW_ERR_FORMAT, "playlist chunk %u is bad", k);
            uint32_t mtot = rd32(b, pos + 8), mtype = rd32(b, pos + 12);
            NEED(mtot >= 24 && mtot <= end - pos, DBW_ERR_FORMAT, "playlist chunk %u has a bad size", k);
            NEED(mtype == 1 || mtype == 100 || mtype == 102 || mtype == 52, DBW_ERR_UNSUPPORTED, "playlist chunk type %u is not known", mtype);
            if (mtype == 52) {
                NEED(pl->n_sorts < MAX_SORTS, DBW_ERR_UNSUPPORTED, "too many sort tables");
                uint32_t st = rd32(b, pos + 24), cnt = rd32(b, pos + 28);
                NEED(st == 3 || st == 4 || st == 5 || st == 7 || st == 18 || st == 35 || st == 36, DBW_ERR_UNSUPPORTED, "sort table type %u is not known", st);
                NEED(cnt == P->n_tracks && mtot == MHOD52_BODY + 4 * (size_t)cnt, DBW_ERR_FORMAT, "sort table has the wrong size");
                memset(seen, 0, P->n_tracks);
                for (uint32_t i = 0; i < cnt; i++) {
                    uint32_t v = rd32(b, pos + MHOD52_BODY + 4 * (size_t)i);
                    NEED(v < cnt && !seen[v], DBW_ERR_FORMAT, "sort table %u is not a valid ordering", st);
                    seen[v] = 1;
                }
                pl->sort_off[pl->n_sorts] = pos; pl->sort_type[pl->n_sorts] = st; pl->n_sorts++;
            }
            pos += mtot;
        }
        pl->first_mhip = pos;

        // playlist items: one per track, in order, each pointing at an existing track exactly once
        memset(seen, 0, P->n_tracks);
        uint32_t prev = 0;
        for (uint32_t i = 0; i < nip; i++) {
            NEED(end - pos >= MHIP_SIZE && memcmp(b + pos, "mhip", 4) == 0, DBW_ERR_FORMAT, "playlist item %u is bad", i);
            NEED(rd32(b, pos + 4) == MHIP_HDR && rd32(b, pos + 8) == MHIP_SIZE && rd32(b, pos + 12) == 1,
                 DBW_ERR_UNSUPPORTED, "playlist item %u has an unknown layout", i);
            uint32_t pid = rd32(b, pos + 20), tid = rd32(b, pos + 24);
            NEED(i == 0 || pid > prev, DBW_ERR_UNSUPPORTED, "playlist item ids are not ascending");
            size_t lo2 = 0, hi2 = P->n_tracks, found = (size_t)-1;
            while (lo2 < hi2) {
                size_t mid = (lo2 + hi2) / 2; uint32_t v = rd32(b, P->track_off[mid] + 16);
                if (v == tid) { found = mid; break; }
                if (v < tid) lo2 = mid + 1; else hi2 = mid;
            }
            NEED(found != (size_t)-1 && !seen[found], DBW_ERR_FORMAT, "playlist item %u points at a missing or repeated track", i);
            NEED(rd64(b, pos + 44) == rd64(b, P->track_off[found] + 112), DBW_ERR_FORMAT, "playlist item %u: persistent id mismatch", i);
            seen[found] = 1;
            NEED(memcmp(b + pos + MHIP_HDR, "mhod", 4) == 0 && rd32(b, pos + MHIP_HDR + 12) == 100 &&
                 rd32(b, pos + MHIP_HDR + 24) == i, DBW_ERR_FORMAT, "playlist item %u has the wrong position", i);
            prev = pid; bump(P, pid);
            pos += MHIP_SIZE;
        }
        NEED(pos == end, DBW_ERR_FORMAT, "playlist has leftover bytes");
    }
    NEED(P->pl[0].n_sorts == P->pl[1].n_sorts, DBW_ERR_UNSUPPORTED, "playlist copies differ");

done:
    free(seen);
    if (rc != DBW_OK) parsed_free(P);
    return rc;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
static int read_file(const char *path, uint8_t **buf, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return DBW_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return DBW_ERR_IO; }
    long sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > 256ul * 1024 * 1024) { fclose(f); return DBW_ERR_FORMAT; }
    rewind(f);
    uint8_t *p = malloc(sz ? (size_t)sz : 1);
    if (p == NULL) { fclose(f); return DBW_ERR_MEMORY; }
    if (fread(p, 1, (size_t)sz, f) != (size_t)sz) { free(p); fclose(f); return DBW_ERR_IO; }
    fclose(f);
    *buf = p; *len = (size_t)sz;
    return DBW_OK;
}

int dbwriter_verify(const char *path, char *err, size_t err_size) {
    char dummy[8];
    if (err == NULL || err_size == 0) { err = dummy; err_size = sizeof(dummy); }
    err[0] = '\0';
    if (path == NULL) return DBW_ERR_NULL;
    uint8_t *b; size_t len;
    int rc = read_file(path, &b, &len);
    if (rc != DBW_OK) { snprintf(err, err_size, "could not read the file"); return rc; }
    parsed_t P;
    rc = verify_buffer(b, len, err, err_size, &P);
    if (rc == DBW_OK) parsed_free(&P);
    free(b);
    return rc;
}

uint32_t dbwriter_mac_time(time_t t) { return (uint32_t)((int64_t)t + 2082844800LL); }

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
static size_t utf8_to_utf16le(const char *s, uint8_t *out) {
    const unsigned char *p = (const unsigned char *)s;
    size_t o = 0;
    while (*p) {
        uint32_t cp; int n;
        if (*p < 0x80) { cp = *p; n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
        else { cp = 0xFFFD; n = 1; }
        int ok = 1;
        for (int i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) { ok = 0; break; }      // also stops at the NUL terminator
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (!ok || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { cp = 0xFFFD; n = ok ? n : 1; }
        p += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            uint32_t hi = 0xD800 + (cp >> 10), lo = 0xDC00 + (cp & 0x3FF);
            out[o++] = (uint8_t)hi; out[o++] = (uint8_t)(hi >> 8);
            out[o++] = (uint8_t)lo; out[o++] = (uint8_t)(lo >> 8);
        } else {
            out[o++] = (uint8_t)cp; out[o++] = (uint8_t)(cp >> 8);
        }
    }
    return o;
}

// A text chunk the way the iPod database stores it
static int put_mhod_string(bytes_t *o, uint32_t type, const char *utf8) {
    size_t cap = 2 * strlen(utf8) + 4;
    uint8_t *tmp = malloc(cap);
    if (tmp == NULL) return -1;
    size_t n = utf8_to_utf16le(utf8, tmp);
    int rc = 0;
    rc |= bt_put(o, "mhod", 4); rc |= bt_put32(o, 24); rc |= bt_put32(o, (uint32_t)(40 + n)); rc |= bt_put32(o, type);
    rc |= bt_put32(o, 0); rc |= bt_put32(o, 0);
    rc |= bt_put32(o, 1); rc |= bt_put32(o, (uint32_t)n); rc |= bt_put32(o, 1); rc |= bt_put32(o, 0);   // utf-16, byte length, 1, 0
    rc |= bt_put(o, tmp, n);
    free(tmp);
    return rc ? -1 : 0;
}

// Read the text of a chunk of the given type inside a track or album record
static int record_string(const uint8_t *b, size_t rec, uint32_t type, char *out, size_t outsz) {
    size_t pos = rec + rd32(b, rec + 4);
    uint32_t nm = rd32(b, rec + 12);
    for (uint32_t k = 0; k < nm; k++) {
        uint32_t mtot = rd32(b, pos + 8);
        if (rd32(b, pos + 12) == type) {
            utf16_to_utf8(b + pos + 40, rd32(b, pos + 28), 0, out, outsz);
            return 1;
        }
        pos += mtot;
    }
    out[0] = '\0';
    return 0;
}

// Compare ignoring ASCII case; other bytes are compared as they are
static int keycmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca < 128) ca = tolower(ca);
        if (cb < 128) cb = tolower(cb);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
}

// The text a given sort table orders by, for an existing track
static void sort_key(const uint8_t *b, size_t rec, uint32_t sort_type, char *out, size_t outsz) {
    static const struct { uint32_t type; uint32_t first, second, third; } MAP[] = {
        {3, 27, 1, 0}, {4, 29, 3, 0}, {5, 28, 4, 0}, {7, 5, 0, 0}, {18, 31, 12, 0}, {35, 30, 22, 4}, {36, 30, 22, 4},
    };
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        if (MAP[i].type != sort_type) continue;
        uint32_t order[3] = {MAP[i].first, MAP[i].second, MAP[i].third};
        for (int k = 0; k < 3; k++) {
            if (order[k] && record_string(b, rec, order[k], out, outsz) && out[0]) return;
        }
        break;
    }
    out[0] = '\0';
}

// Where in a sort table should the new track go? The existing tables are ordered by Apple's
// rules, which are more detailed than a plain comparison, so an exact "sorted" position doesn't always
// exist. Pick the position that disagrees with the fewest existing entries; everything already
// in the table keeps its place.
static size_t sort_position(const uint8_t *b, const parsed_t *P, size_t sort_off, uint32_t sort_type, const char *new_key) {
    size_t n = P->n_tracks;
    char key[512];
    long cost = 0;
    for (size_t i = 0; i < n; i++) {                              // cost of inserting at position 0
        sort_key(b, P->track_off[rd32(b, sort_off + MHOD52_BODY + 4 * i)], sort_type, key, sizeof(key));
        if (keycmp(key, new_key) < 0) cost++;
    }
    long best = cost; size_t best_pos = 0;
    for (size_t i = 0; i < n; i++) {
        sort_key(b, P->track_off[rd32(b, sort_off + MHOD52_BODY + 4 * i)], sort_type, key, sizeof(key));
        int c = keycmp(key, new_key);
        cost += (c > 0) - (c < 0);
        if (cost <= best) { best = cost; best_pos = i + 1; }
    }
    return best_pos;
}

static uint64_t random64(void) {
    uint64_t v = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f != NULL) { if (fread(&v, sizeof(v), 1, f) != 1) v = 0; fclose(f); }
    while (v == 0 || (v >> 32) == 0) v = (v << 13) ^ ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ (uint64_t)time(NULL);
    return v;
}

// ---------------------------------------------------------------------------
// Adding a track
// ---------------------------------------------------------------------------
typedef struct { int kind; size_t off; } op_t;      // kind: 0 albums, 1 tracks, 2 playlist (index in .off2)
static int cmp_op_desc(const void *x, const void *y) {
    const op_t *a = x, *b = y;
    return a->off < b->off ? 1 : a->off > b->off ? -1 : 0;
}

int dbwriter_add_track(const char *in_path, const char *out_path, const dbw_track_t *t, dbw_report_t *rep) {
    dbw_report_t local;
    if (rep == NULL) rep = &local;
    memset(rep, 0, sizeof(*rep));
    if (in_path == NULL || out_path == NULL || t == NULL) return DBW_ERR_NULL;
    if (strcmp(in_path, out_path) == 0) { snprintf(rep->message, sizeof(rep->message), "output must be a different file from the input"); return DBW_ERR_ARG; }

    // The database this writer was built from never has a blank artist or album, so blanks become
    // the usual placeholder names rather than a kind of entry that was never seen to work.
    const char *title = t->title;
    const char *artist = (t->artist && t->artist[0]) ? t->artist : "Unknown Artist";
    const char *album = (t->album && t->album[0]) ? t->album : "Unknown Album";
    const char *albart = (t->album_artist && t->album_artist[0]) ? t->album_artist : artist;
    if (title == NULL || title[0] == '\0' || t->ipod_path == NULL ||
        strncmp(t->ipod_path, PATH_PREFIX, strlen(PATH_PREFIX)) != 0 || strlen(t->ipod_path) > 90 ||
        t->sample_rate == 0 || t->sample_rate > 0xFFFF || t->size_bytes == 0 || t->length_ms == 0 ||
        strlen(title) > 400 || strlen(artist) > 400 || strlen(album) > 400 || strlen(albart) > 400) {
        snprintf(rep->message, sizeof(rep->message), "unusable track details (title, path, sample rate, size and length are required)");
        return DBW_ERR_ARG;
    }

    uint8_t *orig = NULL; size_t olen = 0;
    int rc = read_file(in_path, &orig, &olen);
    if (rc != DBW_OK) { snprintf(rep->message, sizeof(rep->message), "could not read %.120s", in_path); return rc; }

    parsed_t P;
    rc = verify_buffer(orig, olen, rep->message, sizeof(rep->message), &P);
    if (rc != DBW_OK) { free(orig); return rc; }                       // refuse: not a layout we can vouch for
    rep->tracks_before = P.n_tracks; rep->bytes_before = olen;

    bytes_t out = {0}, mhit = {0}, mhia = {0}, mhip = {0};
    char sa[512], sb[512];
    rc = DBW_OK;
#define BAIL(code, ...) do { snprintf(rep->message, sizeof(rep->message), __VA_ARGS__); rc = (code); goto cleanup; } while (0)

    // ---- a typical track to copy the unexplained fields from: unplayed, no artwork, the common text layout ----
    size_t tmpl = (size_t)-1;
    for (size_t i = 0; i < P.n_tracks && tmpl == (size_t)-1; i++) {
        size_t r = P.track_off[i];
        if (rd32(orig, r + 352) || (rd32(orig, r + 124) & 0xFFFF) || rd32(orig, r + 128)) continue;
        if (rd32(orig, r + 76) || rd32(orig, r + 80) || rd32(orig, r + 84) || rd32(orig, r + 88)) continue;
        if (rd32(orig, r + 12) != 6) continue;
        size_t m = r + MHIT_HDR; uint32_t want[6] = {1, 4, 22, 3, 6, 2}; int ok = 1;
        for (int k = 0; k < 6 && ok; k++) { ok = rd32(orig, m + 12) == want[k]; m += rd32(orig, m + 8); }
        if (ok) tmpl = r;
    }
    if (tmpl == (size_t)-1) BAIL(DBW_ERR_UNSUPPORTED, "no suitable existing track to copy from");

    // ---- album and artist: reuse if they exist, otherwise make new ones ----
    uint32_t next = P.max_id + 1;
    uint32_t track_id = next;
    next += 2;                                   // tracks use two ids: their own and id + 1
    uint32_t album_id = 0, artist_id = 0;
    int new_album = 1, new_artist = 1;
    for (size_t i = 0; i < P.n_albums; i++) {
        record_string(orig, P.album_off[i], 200, sa, sizeof(sa));
        record_string(orig, P.album_off[i], 201, sb, sizeof(sb));
        if (strcmp(sa, album) == 0 && strcmp(sb, albart) == 0) { album_id = rd32(orig, P.album_off[i] + 16); new_album = 0; break; }
    }
    for (size_t i = 0; i < P.n_tracks && new_artist; i++) {
        if (record_string(orig, P.track_off[i], 22, sa, sizeof(sa)) && strcmp(sa, albart) == 0) {
            artist_id = rd32(orig, P.track_off[i] + 480); new_artist = 0;
        }
    }
    if (new_album)  album_id = next++;
    if (new_artist) artist_id = next++;
    uint32_t item_id = next++;

    uint64_t dbid = 0;
    for (int tries = 0; tries < 100; tries++) {
        dbid = random64();
        int clash = 0;
        for (size_t i = 0; i < P.n_tracks && !clash; i++) clash = rd64(orig, P.track_off[i] + 112) == dbid;
        if (!clash) break;
        dbid = 0;
    }
    if (dbid == 0) BAIL(DBW_ERR_INTERNAL, "could not make a unique id");

    // ---- the track record ----
    if (bt_put(&mhit, orig + tmpl, MHIT_HDR)) BAIL(DBW_ERR_MEMORY, "out of memory");
    uint8_t *h = mhit.d;
    wr32(h, 16, track_id);
    wr32(h, 28, (rd32(orig, tmpl + 28) & ~1u) | (t->vbr ? 1u : 0u));
    wr32(h, 32, t->modified_time ? t->modified_time : t->added_time);
    wr32(h, 36, t->size_bytes);
    wr32(h, 40, t->length_ms);
    wr32(h, 44, t->track_number);
    wr32(h, 48, t->track_total);
    wr32(h, 52, t->year);
    wr32(h, 56, t->bitrate_kbps);
    wr32(h, 60, t->sample_rate << 16);
    wr32(h, 104, t->added_time);
    wr64(h, 112, dbid);
    { float fsr = (float)t->sample_rate; uint32_t bits; memcpy(&bits, &fsr, 4); wr32(h, 136, bits); }
    wr64(h, 168, dbid);
    wr32(h, 288, album_id);
    wr32(h, 300, t->size_bytes);
    wr32(h, 480, artist_id);
    wr32(h, 500, track_id + 1);
    if (put_mhod_string(&mhit, 1, title) || put_mhod_string(&mhit, 4, artist) || put_mhod_string(&mhit, 22, albart) ||
        put_mhod_string(&mhit, 3, album) || put_mhod_string(&mhit, 6, "MP3") || put_mhod_string(&mhit, 2, t->ipod_path)) {
        BAIL(DBW_ERR_MEMORY, "out of memory");
    }
    wr32(mhit.d, 8, (uint32_t)mhit.n);
    wr32(mhit.d, 12, 6);

    // ---- a new album record, if needed ----
    if (new_album) {
        if (bt_put(&mhia, orig + P.album_off[0], MHIA_HDR)) BAIL(DBW_ERR_MEMORY, "out of memory");
        wr32(mhia.d, 12, 2);
        wr32(mhia.d, 16, album_id);
        wr64(mhia.d, 20, random64());
        wr64(mhia.d, 32, dbid);
        if (put_mhod_string(&mhia, 200, album) || put_mhod_string(&mhia, 201, albart)) BAIL(DBW_ERR_MEMORY, "out of memory");
        wr32(mhia.d, 8, (uint32_t)mhia.n);
    }

    // ---- the playlist entry ----
    if (bt_put(&mhip, orig + P.pl[0].first_mhip, MHIP_SIZE)) BAIL(DBW_ERR_MEMORY, "out of memory");
    wr32(mhip.d, 20, item_id);
    wr32(mhip.d, 24, track_id);
    wr32(mhip.d, 28, t->added_time);
    wr64(mhip.d, 44, dbid);
    wr64(mhip.d, 60, random64());
    wr32(mhip.d, MHIP_HDR + 24, (uint32_t)P.n_tracks);                 // position (counted from 0): the end

    // ---- assemble: make the changes from the end of the file backwards so earlier offsets stay valid ----
    if (bt_put(&out, orig, olen)) BAIL(DBW_ERR_MEMORY, "out of memory");

    op_t ops[4]; int nops = 0;
    ops[nops++] = (op_t){1, P.sec_tracks};
    if (new_album) ops[nops++] = (op_t){0, P.sec_albums};
    ops[nops++] = (op_t){2, P.pl[0].sec};
    ops[nops++] = (op_t){3, P.pl[1].sec};
    qsort(ops, (size_t)nops, sizeof(op_t), cmp_op_desc);

    // where the new track goes in each sort table (decided against the original data)
    size_t ins[2][MAX_SORTS];
    for (int p = 0; p < 2; p++) {
        for (size_t k = 0; k < P.pl[p].n_sorts; k++) {
            uint32_t st = P.pl[p].sort_type[k];
            const char *key = st == 3 ? title : st == 4 ? album : st == 5 ? artist : (st == 35 || st == 36) ? albart : "";
            ins[p][k] = sort_position(orig, &P, P.pl[p].sort_off[k], st, key);
        }
    }

    for (int i = 0; i < nops; i++) {
        size_t so = ops[i].off;
        if (ops[i].kind == 2 || ops[i].kind == 3) {
            int p = ops[i].kind - 2;
            pl_info_t *pl = &P.pl[p];
            if (bt_insert(&out, pl->mhyp + pl->mhyp_total, mhip.d, MHIP_SIZE)) BAIL(DBW_ERR_MEMORY, "out of memory");
            for (size_t k = pl->n_sorts; k-- > 0;) {
                uint8_t v[4]; wr32(v, 0, (uint32_t)P.n_tracks);
                if (bt_insert(&out, pl->sort_off[k] + MHOD52_BODY + 4 * ins[p][k], v, 4)) BAIL(DBW_ERR_MEMORY, "out of memory");
                wr32(out.d, pl->sort_off[k] + 8, rd32(out.d, pl->sort_off[k] + 8) + 4);
                wr32(out.d, pl->sort_off[k] + 28, rd32(out.d, pl->sort_off[k] + 28) + 1);
            }
            uint32_t grow = MHIP_SIZE + 4 * (uint32_t)pl->n_sorts;
            wr32(out.d, pl->mhyp + 8, rd32(out.d, pl->mhyp + 8) + grow);
            wr32(out.d, pl->mhyp + 16, rd32(out.d, pl->mhyp + 16) + 1);
            wr32(out.d, so + 8, rd32(out.d, so + 8) + grow);
        } else if (ops[i].kind == 1) {
            size_t end = so + rd32(out.d, so + 8);
            if (bt_insert(&out, end, mhit.d, mhit.n)) BAIL(DBW_ERR_MEMORY, "out of memory");
            wr32(out.d, so + MHSD_HDR + 8, rd32(out.d, so + MHSD_HDR + 8) + 1);
            wr32(out.d, so + 8, rd32(out.d, so + 8) + (uint32_t)mhit.n);
        } else {
            size_t end = so + rd32(out.d, so + 8);
            if (bt_insert(&out, end, mhia.d, mhia.n)) BAIL(DBW_ERR_MEMORY, "out of memory");
            wr32(out.d, so + MHSD_HDR + 8, rd32(out.d, so + MHSD_HDR + 8) + 1);
            wr32(out.d, so + 8, rd32(out.d, so + 8) + (uint32_t)mhia.n);
        }
    }
    wr32(out.d, 8, (uint32_t)out.n);

    // ---- check the result before it goes anywhere ----
    {
        parsed_t Q; char qerr[200];
        int vr = verify_buffer(out.d, out.n, qerr, sizeof(qerr), &Q);
        if (vr != DBW_OK) BAIL(DBW_ERR_INTERNAL, "the result failed its own check (%.120s); nothing was written", qerr);
        int sane = Q.n_tracks == P.n_tracks + 1 && Q.n_albums == P.n_albums + (size_t)new_album;
        parsed_free(&Q);
        if (!sane) BAIL(DBW_ERR_INTERNAL, "the result has the wrong number of tracks or albums; nothing was written");
    }

    // ---- write to a temporary file, then rename ----
    {
        char tmp[1100];
        if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", out_path) >= sizeof(tmp)) BAIL(DBW_ERR_ARG, "output path too long");
        FILE *f = fopen(tmp, "wb");
        if (f == NULL) BAIL(DBW_ERR_IO, "could not create %.120s", tmp);
        int ok = fwrite(out.d, 1, out.n, f) == out.n;
        ok = (fclose(f) == 0) && ok;
        if (!ok || rename(tmp, out_path) != 0) { remove(tmp); BAIL(DBW_ERR_IO, "could not write %.120s", out_path); }
    }

    rep->track_id = track_id; rep->album_id = album_id;
    rep->new_album = new_album; rep->new_artist = new_artist;
    rep->tracks_after = P.n_tracks + 1; rep->bytes_after = out.n;
    snprintf(rep->message, sizeof(rep->message), "ok");

cleanup:
    free(orig); free(out.d); free(mhit.d); free(mhia.d); free(mhip.d);
    parsed_free(&P);
    return rc;
}
