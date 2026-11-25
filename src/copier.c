#define _POSIX_C_SOURCE 200809L   // opendir, stat, unlink under -std=c11

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "copier.h"

#define MAX_FOLDERS 100          // F00 .. F99
#define NAME_TRIES  50
#define MUSIC_PREFIX ":iPod_Control:Music:"

// Find the F## folders that exist. Returns how many, or -1 if Music can't be opened.
static int find_music_folders(const char *music_dir, int ids[MAX_FOLDERS]) {
    DIR *d = opendir(music_dir);
    if (d == NULL) return -1;

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < MAX_FOLDERS) {
        const char *name = e->d_name;
        if (strlen(name) != 3 || name[0] != 'F' || !isdigit((unsigned char)name[1]) ||
            !isdigit((unsigned char)name[2])) continue;

        char path[1100];
        snprintf(path, sizeof(path), "%s/%s", music_dir, name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            ids[n++] = (name[1] - '0') * 10 + (name[2] - '0');
        }
    }
    closedir(d);
    return n;
}

// Copy src to dst. On any failure the partly written dst is removed.
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (in == NULL) return COPIER_ERR_IO;
    FILE *out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); return COPIER_ERR_IO; }

    static unsigned char buf[64 * 1024];
    int ok = 1;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;          // fclose can fail when the disk is full

    if (!ok) { remove(dst); return COPIER_ERR_IO; }
    return COPIER_OK;
}

int copier_copy_track(const char *ipod_root, const char *src_path, copy_result_t *out) {
    if (ipod_root == NULL || src_path == NULL || out == NULL) return COPIER_ERR_NULL;
    memset(out, 0, sizeof(*out));

    // keep the source's extension (lower-case), so the iPod knows what kind of file it is
    const char *dot = strrchr(src_path, '.');
    const char *slash = strrchr(src_path, '/');
    if (dot == NULL || (slash != NULL && dot < slash) || strlen(dot) < 2 || strlen(dot) > 6) {
        return COPIER_ERR_ARG;
    }
    char ext[8];
    size_t elen = strlen(dot + 1);
    for (size_t i = 0; i < elen; i++) {
        if (!isalnum((unsigned char)dot[1 + i])) return COPIER_ERR_ARG;
        ext[i] = (char)tolower((unsigned char)dot[1 + i]);
    }
    ext[elen] = '\0';

    char music_dir[1024];
    int len = snprintf(music_dir, sizeof(music_dir), "%s/iPod_Control/Music", ipod_root);
    if (len < 0 || (size_t)len >= sizeof(music_dir)) return COPIER_ERR_ARG;

    int ids[MAX_FOLDERS];
    int nfolders = find_music_folders(music_dir, ids);
    if (nfolders <= 0) return COPIER_ERR_NOFOLDER;

    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)clock()); seeded = 1; }

    // pick an unused folder + name combination
    char folder[4], name[5], full[1024], tmp[1040];
    int found = 0;
    for (int attempt = 0; attempt < NAME_TRIES && !found; attempt++) {
        snprintf(folder, sizeof(folder), "F%02d", ids[rand() % nfolders]);
        for (int i = 0; i < 4; i++) name[i] = (char)('A' + rand() % 26);
        name[4] = '\0';

        int fl = snprintf(full, sizeof(full), "%s/%s/%s.%s", music_dir, folder, name, ext);
        if (fl < 0 || (size_t)fl >= sizeof(full)) return COPIER_ERR_ARG;
        snprintf(tmp, sizeof(tmp), "%s.part", full);

        struct stat st;
        if (stat(full, &st) != 0 && stat(tmp, &st) != 0) found = 1;   // neither exists yet
    }
    if (!found) return COPIER_ERR_IO;

    int rc = copy_file(src_path, tmp);
    if (rc != COPIER_OK) return rc;
    if (rename(tmp, full) != 0) { remove(tmp); return COPIER_ERR_IO; }

    snprintf(out->ipod_path, sizeof(out->ipod_path), MUSIC_PREFIX "%s:%s.%s", folder, name, ext);
    snprintf(out->full_path, sizeof(out->full_path), "%s", full);
    return COPIER_OK;
}

int copier_remove_track(const char *ipod_root, const char *ipod_path) {
    if (ipod_root == NULL || ipod_path == NULL) return COPIER_ERR_NULL;

    // only ever delete inside iPod_Control/Music, and never follow ".." or "/"
    if (strncmp(ipod_path, MUSIC_PREFIX, strlen(MUSIC_PREFIX)) != 0) return COPIER_ERR_ARG;
    if (strchr(ipod_path, '/') != NULL || strstr(ipod_path, "..") != NULL ||
        strstr(ipod_path, "::") != NULL) return COPIER_ERR_ARG;

    char rel[96];
    if (strlen(ipod_path) >= sizeof(rel)) return COPIER_ERR_ARG;
    snprintf(rel, sizeof(rel), "%s", ipod_path);
    for (char *c = rel; *c; c++) if (*c == ':') *c = '/';      // ":a:b" -> "/a/b"

    char full[1200];
    int n = snprintf(full, sizeof(full), "%s%s", ipod_root, rel);
    if (n < 0 || (size_t)n >= sizeof(full)) return COPIER_ERR_ARG;

    return unlink(full) == 0 ? COPIER_OK : COPIER_ERR_IO;
}
