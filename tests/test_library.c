#define _POSIX_C_SOURCE 200809L   // for getpid() under -std=c11

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "library.h"

static void touch(const char *path) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs("x", f);
    fclose(f);
}

int main(void) {
    // a folder name unique to this run (mkdtemp is not available on every system)
    char root[64];
    snprintf(root, sizeof root, "/tmp/libtest_%ld", (long)getpid());
    assert(mkdir(root, 0755) == 0);

    char p[1100];
    snprintf(p, sizeof p, "%s/album", root);         mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/a.mp3", root);         touch(p);
    snprintf(p, sizeof p, "%s/b.M4A", root);         touch(p);
    snprintf(p, sizeof p, "%s/notes.txt", root);     touch(p);
    snprintf(p, sizeof p, "%s/album/c.mp3", root);   touch(p);
    snprintf(p, sizeof p, "%s/Band - Tune.mp3", root); touch(p);

    library_t lib;
    assert(library_init(&lib) == LIBRARY_OK);
    assert(library_scan(&lib, root) == LIBRARY_OK);
    assert(lib.count == 4);                           // txt ignored, subfolder found

    // no tags in these files, so details are guessed from the file name
    int found = 0;
    for (size_t i = 0; i < lib.count; i++) {
        if (strstr(lib.tracks[i].path, "Band - Tune.mp3")) {
            assert(strcmp(lib.tracks[i].artist, "Band") == 0);
            assert(strcmp(lib.tracks[i].title, "Tune") == 0);
            found = 1;
        }
        assert(lib.tracks[i].title[0] != '\0');
    }
    assert(found);
    assert(library_scan(&lib, "/no/such/folder") == LIBRARY_ERR_FOLDER);
    assert(library_scan(NULL, root) == LIBRARY_ERR_NULL);
    library_free(&lib);
    library_free(&lib);                               // double free is safe

    snprintf(p, sizeof p, "rm -rf %s", root);
    system(p);
    puts("library tests passed");
    return 0;
}
