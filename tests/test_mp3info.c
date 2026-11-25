#define _POSIX_C_SOURCE 200809L   // truncate() under -std=c11
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "mp3info.h"

// Builds fake MP3 files out of correctly sized, otherwise empty frames.
// (Real MP3s are the real test; these check the arithmetic and the header handling.)

static void write_mp3(const char *path, int id3v2_bytes, int nframes, int frame_len, const uint8_t hdr[4],
                      const char *xing_tag, uint32_t xing_frames, int side_info, int id3v1) {
    FILE *f = fopen(path, "wb"); assert(f);
    if (id3v2_bytes > 0) {
        uint8_t h[10] = {'I','D','3',3,0,0, 0,0,(uint8_t)((id3v2_bytes >> 7) & 0x7F),(uint8_t)(id3v2_bytes & 0x7F)};
        fwrite(h, 1, 10, f);
        for (int i = 0; i < id3v2_bytes; i++) fputc(0, f);
    }
    uint8_t *fr = calloc(1, (size_t)frame_len);
    for (int i = 0; i < nframes; i++) {
        memset(fr, 0, (size_t)frame_len);
        memcpy(fr, hdr, 4);
        if (i == 0 && xing_tag) {
            memcpy(fr + 4 + side_info, xing_tag, 4);
            fr[4 + side_info + 7] = 1;                              // flags: frame count present
            fr[4 + side_info + 8]  = (uint8_t)(xing_frames >> 24);
            fr[4 + side_info + 9]  = (uint8_t)(xing_frames >> 16);
            fr[4 + side_info + 10] = (uint8_t)(xing_frames >> 8);
            fr[4 + side_info + 11] = (uint8_t)xing_frames;
        }
        fwrite(fr, 1, (size_t)frame_len, f);
    }
    free(fr);
    if (id3v1) { uint8_t t[128] = {'T','A','G'}; fwrite(t, 1, 128, f); }
    fclose(f);
}

int main(void) {
    const char *p = "/tmp/test_mp3info.mp3";
    mp3info_t m;

    // MPEG-1 Layer III, 128 kbps, 44.1 kHz, stereo, no padding: frame = 144*128000/44100 = 417 bytes
    const uint8_t h128[4] = {0xFF, 0xFB, 0x90, 0x00};
    write_mp3(p, 0, 1000, 417, h128, NULL, 0, 32, 0);
    assert(mp3info_read(p, &m) == MP3INFO_OK);
    assert(m.sample_rate == 44100 && m.bitrate_kbps == 128 && m.channels == 2 && m.vbr == 0);
    assert(m.length_ms == 1000u * 417u * 8u / 128u);                                                  // CBR: 26,062 ms, from the size
    assert(m.size_bytes == 417000);

    // with an ID3v2 tag in front and an ID3v1 tag behind: both must be skipped
    write_mp3(p, 2000, 1000, 417, h128, NULL, 0, 32, 1);
    assert(mp3info_read(p, &m) == MP3INFO_OK);
    assert(m.length_ms == 1000u * 417u * 8u / 128u && m.bitrate_kbps == 128);

    // Xing (VBR) header: length comes from the frame count, bitrate is the average
    write_mp3(p, 0, 500, 417, h128, "Xing", 2000, 32, 0);
    assert(mp3info_read(p, &m) == MP3INFO_OK);
    assert(m.vbr == 1);
    assert(m.length_ms == (uint32_t)(2000ull * 1152 * 1000 / 44100));                                 // 52244 ms
    assert(m.bitrate_kbps == (uint32_t)((500ull * 417 * 8) / m.length_ms));

    // "Info" = constant bitrate with an exact frame count
    write_mp3(p, 0, 500, 417, h128, "Info", 500, 32, 0);
    assert(mp3info_read(p, &m) == MP3INFO_OK);
    assert(m.vbr == 0 && m.length_ms == (uint32_t)(500ull * 1152 * 1000 / 44100));

    // 48 kHz, 320 kbps: frame = 144*320000/48000 = 960
    const uint8_t h320[4] = {0xFF, 0xFB, 0xE4, 0x00};
    write_mp3(p, 0, 300, 960, h320, NULL, 0, 32, 0);
    assert(mp3info_read(p, &m) == MP3INFO_OK && m.sample_rate == 48000 && m.bitrate_kbps == 320);

    // MPEG-2, mono, 64 kbps, 22.05 kHz: frame = 72*64000/22050 = 208
    const uint8_t h2[4] = {0xFF, 0xF3, 0x80, 0xC0};
    write_mp3(p, 0, 400, 208, h2, NULL, 0, 9, 0);
    assert(mp3info_read(p, &m) == MP3INFO_OK && m.sample_rate == 22050 && m.channels == 1 && m.bitrate_kbps == 64);

    // not audio / missing / bad arguments
    { FILE *f = fopen(p, "wb"); for (int i = 0; i < 5000; i++) fputc('A', f); fclose(f); }
    assert(mp3info_read(p, &m) == MP3INFO_ERR_FORMAT);
    assert(mp3info_read("/no/such.mp3", &m) == MP3INFO_ERR_IO);
    assert(mp3info_read(NULL, &m) == MP3INFO_ERR_NULL);
    assert(mp3info_read(p, NULL) == MP3INFO_ERR_NULL);
    { FILE *f = fopen(p, "wb"); fclose(f); }
    assert(mp3info_read(p, &m) == MP3INFO_ERR_IO);                                                    // empty file

    // damaged data must be handled safely
    srand(7);
    for (int i = 0; i < 3000; i++) {
        write_mp3(p, 100, 20, 417, h128, "Xing", 20, 32, 1);
        FILE *f = fopen(p, "r+b"); assert(f);
        fseek(f, 0, SEEK_END); long sz = ftell(f);
        for (int k = 0; k < 1 + rand() % 6; k++) { fseek(f, rand() % sz, SEEK_SET); fputc(rand() & 0xFF, f); }
        fclose(f);
        if (rand() % 4 == 0) { f = fopen(p, "r+b"); fseek(f, 0, SEEK_END); (void)!truncate(p, rand() % sz); fclose(f); }
        int rc = mp3info_read(p, &m);
        assert(rc == MP3INFO_OK || rc == MP3INFO_ERR_FORMAT || rc == MP3INFO_ERR_IO);
    }

    remove(p);
    puts("mp3info tests passed");
    return 0;
}
