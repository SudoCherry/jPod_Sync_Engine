#ifndef TEXTCONV_H
#define TEXTCONV_H

#include <stddef.h>
#include <stdint.h>

// Text conversion helpers shared by the database and tag readers.
// All of them always leave `out` as a valid, NUL-terminated UTF-8 string
// (cut short if it doesn't fit) and never write past outsz bytes.

// UTF-16 (little- or big-endian) -> UTF-8. nbytes is the byte length of the input.
void utf16_to_utf8(const uint8_t *in, size_t nbytes, int big_endian, char *out, size_t outsz);

// ISO-8859-1 ("Latin-1") -> UTF-8
void latin1_to_utf8(const uint8_t *in, size_t nbytes, char *out, size_t outsz);

#endif
