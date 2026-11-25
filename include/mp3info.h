#ifndef MP3INFO_H
#define MP3INFO_H

#include <stdint.h>

#define MP3INFO_OK            0
#define MP3INFO_ERR_NULL     -1
#define MP3INFO_ERR_IO       -2   // file could not be opened / read
#define MP3INFO_ERR_FORMAT   -3   // no usable MPEG Layer III audio found

// What the iPod database needs to know about an MP3 file
typedef struct {
    uint32_t size_bytes;      // whole file size
    uint32_t length_ms;       // play time
    uint32_t bitrate_kbps;    // average bitrate
    uint32_t sample_rate;     // Hz, e.g. 44100
    int      channels;        // 1 or 2
    int      vbr;             // 1 if variable bitrate
} mp3info_t;

// Read the first audio frame (and a Xing/Info/VBRI header if present) to work out
// the play time and bitrate. Skips an ID3v2 tag at the start and ID3v1 at the end.
// Only MPEG audio Layer III ("MP3") is supported.
int mp3info_read(const char *path, mp3info_t *out);

#endif
