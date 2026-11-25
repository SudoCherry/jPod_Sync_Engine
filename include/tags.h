#ifndef TAGS_H
#define TAGS_H

#define TAGS_OK            0    // found at least a title, artist or album
#define TAGS_ERR_NULL     -1
#define TAGS_ERR_IO       -2    // file could not be opened
#define TAGS_ERR_NONE     -3    // file opened fine but has no readable tags

#define TAG_STR_MAX 256

// The text details of one song, all UTF-8. Empty string = not known.
typedef struct {
    char title[TAG_STR_MAX];
    char artist[TAG_STR_MAX];
    char album[TAG_STR_MAX];
} tags_t;

// Read title / artist / album from an MP3 file's ID3 tags.
// Supports ID3v2.2, v2.3 and v2.4 (found at the start of the file) and falls
// back to ID3v1 (the last 128 bytes). Other formats (e.g. .m4a) simply report
// TAGS_ERR_NONE for now. `out` is always left in a valid state.
int tags_read(const char *path, tags_t *out);

// Guess details from a file name: "Artist - Title.mp3" -> artist + title,
// anything else -> title only (the name without its extension).
void tags_from_filename(const char *path, tags_t *out);

#endif
