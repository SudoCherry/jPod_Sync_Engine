#include "textconv.h"
#include <string.h>

// Write one code point as UTF-8 into tmp (4 bytes max). Returns how many bytes.
static size_t encode_utf8(uint32_t cp, char *tmp) {
    if (cp < 0x80)    { tmp[0] = (char)cp; return 1; }
    if (cp < 0x800)   { tmp[0] = (char)(0xC0 | (cp >> 6));
                        tmp[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { tmp[0] = (char)(0xE0 | (cp >> 12));
                        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        tmp[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    tmp[0] = (char)(0xF0 | (cp >> 18));
    tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    tmp[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

void utf16_to_utf8(const uint8_t *in, size_t nbytes, int big_endian, char *out, size_t outsz) {
    if (outsz == 0) return;
    size_t o = 0;
    size_t units = nbytes / 2;

    for (size_t i = 0; i < units; i++) {
        #define UNIT(k) (big_endian ? (uint32_t)(((uint32_t)in[2*(k)] << 8) | in[2*(k)+1]) \
                                    : (uint32_t)(((uint32_t)in[2*(k)+1] << 8) | in[2*(k)]))
        uint32_t cp = UNIT(i);

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units) {          // high surrogate
            uint32_t lo = UNIT(i + 1);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        #undef UNIT
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   // unpaired surrogate -> replacement char

        char tmp[4];
        size_t n = encode_utf8(cp, tmp);
        if (o + n + 1 > outsz) break;                    // keep room for the terminator
        memcpy(out + o, tmp, n);
        o += n;
    }
    out[o] = '\0';
}

void latin1_to_utf8(const uint8_t *in, size_t nbytes, char *out, size_t outsz) {
    if (outsz == 0) return;
    size_t o = 0;
    for (size_t i = 0; i < nbytes; i++) {
        char tmp[4];
        size_t n = encode_utf8(in[i], tmp);
        if (o + n + 1 > outsz) break;
        memcpy(out + o, tmp, n);
        o += n;
    }
    out[o] = '\0';
}
