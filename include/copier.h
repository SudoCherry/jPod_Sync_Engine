#ifndef COPIER_H
#define COPIER_H

#define COPIER_OK            0
#define COPIER_ERR_NULL     -1
#define COPIER_ERR_IO       -2   // could not read the source / write the iPod / file missing
#define COPIER_ERR_NOFOLDER -3   // <ipod>/iPod_Control/Music (with F00, F01... inside) not found
#define COPIER_ERR_ARG      -4   // unusable file name or path

// Where a copied track ended up
typedef struct {
    char ipod_path[96];      // the form the database stores, e.g. ":iPod_Control:Music:F07:ABCD.mp3"
    char full_path[1024];    // the real file, e.g. "/Volumes/PHOTO IPOD/iPod_Control/Music/F07/ABCD.mp3"
} copy_result_t;

// Copy one music file onto the iPod, into a random F## folder under a random
// 4-letter name (the way iTunes lays files out). It never creates folders:
// if iPod_Control/Music/F## doesn't exist, ipod_root is probably the wrong place.
// The file is copied to a temporary name and renamed only when complete, so an
// interrupted copy never leaves a half-written song that looks real.
int copier_copy_track(const char *ipod_root, const char *src_path, copy_result_t *out);

// Delete a track previously copied, given its database-style path
// (":iPod_Control:Music:F07:ABCD.mp3"). Refuses any path outside iPod_Control/Music.
// Use it to undo a copy if updating the database fails afterwards.
int copier_remove_track(const char *ipod_root, const char *ipod_path);

#endif
