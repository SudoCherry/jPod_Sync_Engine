#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tags.h"
#include "textconv.h"

#define ID3V2_MAX (16u * 1024u * 1024u)   // ignore absurdly large tags

// ---------- small byte helpers ----------

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t be24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
// ID3v2 sizes use only 7 bits of each byte so no byte is ever 0xFF
static uint32_t syncsafe(const uint8_t *p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7)  |  (uint32_t)(p[3] & 0x7F);
}

// Undo "unsynchronisation": every FF 00 becomes FF. Returns the new length.
static size_t unsync_inplace(uint8_t *b, size_t n) {
    size_t w = 0;
    for (size_t r = 0; r < n; r++) {
        b[w++] = b[r];
        if (b[r] == 0xFF && r + 1 < n && b[r + 1] == 0x00) r++;
    }
    return w;
}

// Copy UTF-8 bytes into out, cutting on a character boundary if it doesn't fit
static void copy_utf8(const uint8_t *s, size_t n, char *out, size_t outsz) {
    if (outsz == 0) return;
    if (n > outsz - 1) {
        n = outsz - 1;
        while (n > 0 && (s[n] & 0xC0) == 0x80) n--;   // don't leave half a character
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

// ---------- decoding one text frame ----------

static size_t find_nul8(const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) if (s[i] == 0) return i;
    return n;
}
static size_t find_nul16(const uint8_t *s, size_t n) {
    for (size_t i = 0; i + 1 < n; i += 2) if (s[i] == 0 && s[i + 1] == 0) return i;
    return n & ~(size_t)1;
}

// A text frame is: 1 encoding byte, then the text. Only the first string is kept.
static void decode_text(const uint8_t *d, size_t len, char *out, size_t outsz) {
    out[0] = '\0';
    if (len < 2) return;
    uint8_t enc = d[0];
    const uint8_t *text = d + 1;
    size_t n = len - 1;

    switch (enc) {
        case 0:                                   // ISO-8859-1
            latin1_to_utf8(text, find_nul8(text, n), out, outsz);
            break;
        case 3:                                   // UTF-8
            copy_utf8(text, find_nul8(text, n), out, outsz);
            break;
        case 1: {                                 // UTF-16 with a byte-order mark
            int big = 0;
            if (n >= 2 && text[0] == 0xFF && text[1] == 0xFE) { text += 2; n -= 2; }
            else if (n >= 2 && text[0] == 0xFE && text[1] == 0xFF) { text += 2; n -= 2; big = 1; }
            utf16_to_utf8(text, find_nul16(text, n), big, out, outsz);
            break;
        }
        case 2:                                   // UTF-16, big-endian, no byte-order mark
            utf16_to_utf8(text, find_nul16(text, n), 1, out, outsz);
            break;
        default:
            break;                                // unknown encoding: ignore the frame
    }
}

// ---------- ID3v2 (start of the file) ----------

static void parse_frames(uint8_t *buf, size_t size, unsigned major, size_t pos, tags_t *t) {
    char *dest[3] = { t->title, t->artist, t->album };
    size_t hdr_len = (major == 2) ? 6 : 10;

    while (size - pos >= hdr_len) {                       // pos <= size always holds here
        const uint8_t *id = buf + pos;
        if (id[0] == 0) break;                            // padding: no more frames

        size_t fsize;
        unsigned fl = 0;
        if (major == 2)      fsize = be24(id + 3);
        else if (major == 3) { fsize = be32(id + 4); fl = id[9]; }
        else                 { fsize = syncsafe(id + 4); fl = id[9]; }

        size_t data = pos + hdr_len;
        if (fsize > size - data) break;                   // frame runs past the tag: stop
        size_t next = data + fsize;                       // where the following frame starts

        // which of the three fields is this frame?
        int which = 0;
        if (major == 2) {
            if (!memcmp(id, "TT2", 3)) which = 1;
            else if (!memcmp(id, "TP1", 3)) which = 2;
            else if (!memcmp(id, "TAL", 3)) which = 3;
        } else {
            if (!memcmp(id, "TIT2", 4)) which = 1;
            else if (!memcmp(id, "TPE1", 4)) which = 2;
            else if (!memcmp(id, "TALB", 4)) which = 3;
        }

        if (which && dest[which - 1][0] == '\0') {
            uint8_t *d = buf + data;
            size_t len = fsize;
            int usable = 1;

            if (major == 3) {
                if (fl & 0xC0) usable = 0;                // compressed or encrypted
                else if (fl & 0x20) { if (len >= 1) { d++; len--; } else usable = 0; }   // group id byte
            } else if (major == 4) {
                if (fl & 0x0C) usable = 0;                // compressed or encrypted
                if (usable && (fl & 0x40)) { if (len >= 1) { d++; len--; } else usable = 0; }
                if (usable && (fl & 0x01)) { if (len >= 4) { d += 4; len -= 4; } else usable = 0; }
                if (usable && (fl & 0x02)) len = unsync_inplace(d, len);
            }
            if (usable) decode_text(d, len, dest[which - 1], TAG_STR_MAX);
        }
        pos = next;
    }
}

static void read_id3v2(FILE *f, tags_t *t) {
    uint8_t hdr[10];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(hdr, 1, 10, f) != 10) return;
    if (memcmp(hdr, "ID3", 3) != 0) return;

    unsigned major = hdr[3], flags = hdr[5];
    if (major < 2 || major > 4) return;
    if ((hdr[6] | hdr[7] | hdr[8] | hdr[9]) & 0x80) return;       // not a valid syncsafe size

    size_t size = syncsafe(hdr + 6);
    if (size == 0 || size > ID3V2_MAX) return;

    uint8_t *buf = malloc(size);
    if (buf == NULL) return;
    size = fread(buf, 1, size, f);                                // use whatever was really there

    if (flags & 0x80) size = unsync_inplace(buf, size);           // whole-tag unsynchronisation

    size_t pos = 0;
    int ok = 1;
    if (flags & 0x40) {
        if (major == 2) {
            ok = 0;                                               // compressed v2.2 tag: unsupported
        } else if (size < 4) {
            ok = 0;
        } else {
            // v2.4 stores the extended header's size including itself, v2.3 excluding
            uint64_t ext = (major == 4) ? syncsafe(buf) : (uint64_t)be32(buf) + 4;
            if (ext > size) ok = 0; else pos = (size_t)ext;
        }
    }
    if (ok) parse_frames(buf, size, major, pos, t);
    free(buf);
}

// ---------- ID3v1 (last 128 bytes) ----------

static void v1_field(const uint8_t *src, char *dest) {
    if (dest[0] != '\0') return;                                  // v2 already supplied it
    size_t n = find_nul8(src, 30);
    while (n > 0 && src[n - 1] == ' ') n--;
    latin1_to_utf8(src, n, dest, TAG_STR_MAX);
}

static void read_id3v1(FILE *f, tags_t *t) {
    uint8_t b[128];
    if (fseek(f, -128, SEEK_END) != 0 || fread(b, 1, 128, f) != 128) return;
    if (memcmp(b, "TAG", 3) != 0) return;
    v1_field(b + 3, t->title);
    v1_field(b + 33, t->artist);
    v1_field(b + 63, t->album);
}

// ---------- public functions ----------

int tags_read(const char *path, tags_t *out) {
    if (out == NULL) return TAGS_ERR_NULL;
    memset(out, 0, sizeof(*out));
    if (path == NULL) return TAGS_ERR_NULL;

    FILE *f = fopen(path, "rb");
    if (f == NULL) return TAGS_ERR_IO;

    read_id3v2(f, out);
    if (out->title[0] == '\0' || out->artist[0] == '\0' || out->album[0] == '\0') {
        read_id3v1(f, out);
    }
    fclose(f);

    return (out->title[0] || out->artist[0] || out->album[0]) ? TAGS_OK : TAGS_ERR_NONE;
}

void tags_from_filename(const char *path, tags_t *out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (path == NULL) return;

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    size_t len = strlen(name);
    const char *dot = strrchr(name, '.');
    if (dot != NULL && dot != name) len = (size_t)(dot - name);   // drop ".mp3"

    const char *sep = NULL;                                       // look for " - "
    for (size_t i = 0; i + 3 <= len; i++) {
        if (name[i] == ' ' && name[i + 1] == '-' && name[i + 2] == ' ') { sep = name + i; break; }
    }

    if (sep != NULL && sep != name && (size_t)(sep - name) + 3 < len) {
        copy_utf8((const uint8_t *)name, (size_t)(sep - name), out->artist, TAG_STR_MAX);
        copy_utf8((const uint8_t *)(sep + 3), len - (size_t)(sep - name) - 3, out->title, TAG_STR_MAX);
    } else {
        copy_utf8((const uint8_t *)name, len, out->title, TAG_STR_MAX);
    }
}
