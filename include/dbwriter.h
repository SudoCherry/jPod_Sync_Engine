#ifndef DBWRITER_H
#define DBWRITER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define DBW_OK                0
#define DBW_ERR_NULL         -1
#define DBW_ERR_IO           -2   // a file could not be read or written
#define DBW_ERR_FORMAT       -3   // not an iTunesDB, or it is internally inconsistent
#define DBW_ERR_UNSUPPORTED  -4   // consistent, but not the layout this writer knows. Nothing is written
#define DBW_ERR_ARG          -5   // unusable track details
#define DBW_ERR_MEMORY       -6
#define DBW_ERR_INTERNAL     -7   // the finished result failed its own checks. Nothing is written

// One track to add. All text is UTF-8.
typedef struct {
    const char *title;            // required
    const char *artist;           // NULL or "" becomes "Unknown Artist"
    const char *album;            // NULL or "" becomes "Unknown Album"
    const char *album_artist;     // NULL = same as artist
    const char *ipod_path;        // ":iPod_Control:Music:F07:ABCD.mp3" (from copier_copy_track)
    uint32_t size_bytes;
    uint32_t length_ms;
    uint32_t bitrate_kbps;
    uint32_t sample_rate;         // Hz
    int      vbr;
    uint32_t track_number;        // 0 = unknown
    uint32_t track_total;         // 0 = unknown
    uint32_t year;                // 0 = unknown
    uint32_t added_time;          // seconds since 1904-01-01 (see dbwriter_mac_time)
    uint32_t modified_time;
} dbw_track_t;

// What happened
typedef struct {
    uint32_t track_id;
    uint32_t album_id;
    int      new_album;           // 1 if a new album entry was created
    int      new_artist;          // 1 if a new artist id was created
    size_t   tracks_before, tracks_after;
    size_t   bytes_before, bytes_after;
    char     message[200];        // reason, when something failed
} dbw_report_t;

// Read in_path (an iTunesDB), add the track, and write the result to out_path.
//  * in_path is never modified, and out_path must be a different file.
//  * The input is checked first and refused if it isn't exactly the layout this
//    writer understands; the result is checked again before anything is written.
//  * The output is written to a temporary name and renamed when complete.
int dbwriter_add_track(const char *in_path, const char *out_path, const dbw_track_t *track, dbw_report_t *report);

// Check a database file's structure: sizes, counts, ids, references and sort tables.
// Returns DBW_OK, or an error code with a description in err.
int dbwriter_verify(const char *path, char *err, size_t err_size);

// Unix time -> seconds since 1904-01-01 (the iPod's clock)
uint32_t dbwriter_mac_time(time_t unix_time);

#endif
