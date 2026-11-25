// Small command-line tool for trying the copier by hand:
//   ./copier_demo <ipod_root> <music_file>
// Copies the file into <ipod_root>/iPod_Control/Music/F##/ and prints where it went.

#include <stdio.h>
#include "copier.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <ipod_root> <music_file>\n", argv[0]);
        return 2;
    }

    copy_result_t r;
    int rc = copier_copy_track(argv[1], argv[2], &r);
    if (rc != COPIER_OK) {
        printf("Copy failed (error %d)%s\n", rc,
               rc == COPIER_ERR_NOFOLDER ? ": no iPod_Control/Music/F## folders under that path" : "");
        return 1;
    }

    printf("Copied to:      %s\n", r.full_path);
    printf("Database path:  %s\n", r.ipod_path);
    return 0;
}
