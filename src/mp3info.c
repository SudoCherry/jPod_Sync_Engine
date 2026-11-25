#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp3info.h"

// bitrate tables (kbps) for Layer III; index 0 = "free", 15 = invalid
static const int BR_V1_L3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1};
static const int BR_V2_L3[16] = {0,  8, 16, 24, 32, 40, 48, 56,  64,  80,  96, 112, 128, 144, 160, -1};
static const int SR_V1[4]  = {44100, 48000, 32000, 0};
static const int SR_V2[4]  = {22050, 24000, 16000, 0};
static const int SR_V25[4] = {11025, 12000,  8000, 0};

typedef struct {
    int mpeg1;            // 1 = MPEG-1, 0 = MPEG-2 / 2.5
    int bitrate_kbps;
    int sample_rate;
    int channels;
    int padding;
    int crc;              // 1 if a 2-byte CRC follows the header
    int samples_per_frame;
    int frame_bytes;
} frame_t;

// Decode a 4-byte frame header. Returns 0 if it isn't a valid Layer III header.
static int parse_header(const uint8_t *h, frame_t *f) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return -1;     // 11 sync bits
    int ver   = (h[1] >> 3) & 3;                              // 0=2.5, 1=reserved, 2=MPEG2, 3=MPEG1
    int layer = (h[1] >> 1) & 3;                              // 1 = Layer III
    if (ver == 1 || layer != 1) return -1;
    int bri = (h[2] >> 4) & 15, sri = (h[2] >> 2) & 3;
    if (bri == 0 || bri == 15 || sri == 3) return -1;

    f->mpeg1 = (ver == 3);
    f->bitrate_kbps = f->mpeg1 ? BR_V1_L3[bri] : BR_V2_L3[bri];
    f->sample_rate = ver == 3 ? SR_V1[sri] : ver == 2 ? SR_V2[sri] : SR_V25[sri];
    f->padding = (h[2] >> 1) & 1;
    f->crc = !(h[1] & 1);
    f->channels = ((h[3] >> 6) & 3) == 3 ? 1 : 2;
    f->samples_per_frame = f->mpeg1 ? 1152 : 576;
    f->frame_bytes = (f->mpeg1 ? 144 : 72) * f->bitrate_kbps * 1000 / f->sample_rate + f->padding;
    return f->bitrate_kbps > 0 && f->sample_rate > 0 && f->frame_bytes > 4 ? 0 : -1;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int mp3info_read(const char *path, mp3info_t *out) {
    if (out == NULL) return MP3INFO_ERR_NULL;
    memset(out, 0, sizeof(*out));
    if (path == NULL) return MP3INFO_ERR_NULL;

    FILE *f = fopen(path, "rb");
    if (f == NULL) return MP3INFO_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return MP3INFO_ERR_IO; }
    long fsize = ftell(f);
    if (fsize <= 0 || (unsigned long)fsize > 0xFFFFFFFFul) { fclose(f); return MP3INFO_ERR_IO; }

    // where the audio starts: after an ID3v2 tag, if there is one
    long audio_start = 0;
    uint8_t id3[10];
    rewind(f);
    if (fread(id3, 1, 10, f) == 10 && memcmp(id3, "ID3", 3) == 0 &&
        !((id3[6] | id3[7] | id3[8] | id3[9]) & 0x80)) {
        long sz = ((long)id3[6] << 21) | ((long)id3[7] << 14) | ((long)id3[8] << 7) | id3[9];
        audio_start = 10 + sz + ((id3[5] & 0x10) ? 10 : 0);     // footer flag
    }

    // where it ends: before an ID3v1 tag
    long audio_end = fsize;
    uint8_t tag[3];
    if (fsize >= 128 && fseek(f, fsize - 128, SEEK_SET) == 0 && fread(tag, 1, 3, f) == 3 &&
        memcmp(tag, "TAG", 3) == 0) {
        audio_end = fsize - 128;
    }
    if (audio_start >= audio_end) { fclose(f); return MP3INFO_ERR_FORMAT; }

    // find the first frame: look for a valid header that is followed by another one
    // (a single match could be a coincidence inside tag or junk data)
    enum { SCAN_MAX = 256 * 1024 };
    long scan_len = audio_end - audio_start;
    if (scan_len > SCAN_MAX) scan_len = SCAN_MAX;
    uint8_t *buf = malloc((size_t)scan_len);
    if (buf == NULL) { fclose(f); return MP3INFO_ERR_IO; }
    if (fseek(f, audio_start, SEEK_SET) != 0 || fread(buf, 1, (size_t)scan_len, f) != (size_t)scan_len) {
        free(buf); fclose(f); return MP3INFO_ERR_IO;
    }
    fclose(f);

    frame_t fr;
    long pos = -1;
    for (long i = 0; i + 4 <= scan_len; i++) {
        if (parse_header(buf + i, &fr) != 0) continue;
        frame_t next;
        long n = i + fr.frame_bytes;
        // accept if the next frame also parses with the same sample rate, or this is the last frame
        if ((n + 4 <= scan_len && parse_header(buf + n, &next) == 0 && next.sample_rate == fr.sample_rate &&
             next.mpeg1 == fr.mpeg1) || n + 4 > scan_len) {
            pos = i;
            break;
        }
    }
    if (pos < 0) { free(buf); return MP3INFO_ERR_FORMAT; }
    parse_header(buf + pos, &fr);

    // Xing / Info (at a fixed offset after the side info) or VBRI (always 32 bytes after the header)
    long hdr_end = pos + 4 + (fr.crc ? 2 : 0);
    int side = fr.mpeg1 ? (fr.channels == 1 ? 17 : 32) : (fr.channels == 1 ? 9 : 17);
    long xo = hdr_end + side;
    long frames = -1;
    int vbr = 0;
    if (xo + 12 <= scan_len && (memcmp(buf + xo, "Xing", 4) == 0 || memcmp(buf + xo, "Info", 4) == 0)) {
        vbr = (memcmp(buf + xo, "Xing", 4) == 0);
        uint32_t flags = be32(buf + xo + 4);
        if ((flags & 1) && xo + 12 <= scan_len) frames = be32(buf + xo + 8);
    } else if (pos + 36 + 18 <= scan_len && memcmp(buf + pos + 36, "VBRI", 4) == 0) {
        vbr = 1;
        frames = be32(buf + pos + 36 + 14);
    }
    free(buf);

    uint64_t audio_bytes = (uint64_t)(audio_end - audio_start - pos);
    out->size_bytes = (uint32_t)fsize;
    out->sample_rate = (uint32_t)fr.sample_rate;
    out->channels = fr.channels;
    out->vbr = vbr;

    if (frames > 0) {
        out->length_ms = (uint32_t)((uint64_t)frames * fr.samples_per_frame * 1000 / fr.sample_rate);
    } else {
        // constant bitrate: length follows from the size
        out->length_ms = (uint32_t)(audio_bytes * 8 / (uint64_t)fr.bitrate_kbps);   // bytes*8 / (kbps) = ms
    }
    if (out->length_ms == 0) return MP3INFO_ERR_FORMAT;

    out->bitrate_kbps = vbr || frames > 0
        ? (uint32_t)(audio_bytes * 8 / out->length_ms)
        : (uint32_t)fr.bitrate_kbps;
    return MP3INFO_OK;
}
