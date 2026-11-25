#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "copier.h"

static int count_suffix(const char *dir, const char *suffix) {
    DIR *d = opendir(dir); assert(d);
    int n = 0; struct dirent *e;
    size_t sl = strlen(suffix);
    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (l >= sl && strcmp(e->d_name + l - sl, suffix) == 0) n++;
    }
    closedir(d);
    return n;
}

int main(void) {
    char root[64], src[96], p[1200], cmd[1300];
    snprintf(root, sizeof root, "/tmp/copiertest_%ld", (long)getpid());
    snprintf(src, sizeof src, "/tmp/copiertest_src_%ld.MP3", (long)getpid());

    // a pretend iPod: Music/F00..F02, plus things that must be ignored
    assert(mkdir(root, 0755) == 0);
    snprintf(p, sizeof p, "%s/iPod_Control", root);        mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/iPod_Control/Music", root);  mkdir(p, 0755);
    for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/iPod_Control/Music/F%02d", root, i); mkdir(p, 0755); }
    snprintf(p, sizeof p, "%s/iPod_Control/Music/junk", root);  mkdir(p, 0755);          // not F##
    snprintf(p, sizeof p, "%s/iPod_Control/Music/F03", root);   { FILE *f = fopen(p, "w"); fputs("x", f); fclose(f); }  // F## but a file

    // a 300 KB source file with a recognisable pattern
    unsigned char *data = malloc(300000);
    for (int i = 0; i < 300000; i++) data[i] = (unsigned char)(i * 31 + i / 251);
    { FILE *f = fopen(src, "wb"); assert(f); fwrite(data, 1, 300000, f); fclose(f); }

    // --- one copy: name shape, location, contents ---
    copy_result_t r;
    assert(copier_copy_track(root, src, &r) == COPIER_OK);
    assert(strlen(r.ipod_path) == 32);
    assert(strncmp(r.ipod_path, ":iPod_Control:Music:F0", 22) == 0);
    assert(r.ipod_path[22] >= '0' && r.ipod_path[22] <= '2');                 // only the real folders
    assert(r.ipod_path[23] == ':');
    for (int i = 24; i < 28; i++) assert(r.ipod_path[i] >= 'A' && r.ipod_path[i] <= 'Z');
    assert(strcmp(r.ipod_path + 28, ".mp3") == 0);                            // ".MP3" became ".mp3"
    {
        FILE *f = fopen(r.full_path, "rb"); assert(f);
        unsigned char *back = malloc(300001);
        size_t n = fread(back, 1, 300001, f); fclose(f);
        assert(n == 300000 && memcmp(back, data, 300000) == 0);
        free(back);
    }

    // --- many copies: every name unique, no leftover .part files ---
    static char seen[200][96];
    for (int i = 0; i < 200; i++) {
        copy_result_t c;
        assert(copier_copy_track(root, src, &c) == COPIER_OK);
        snprintf(seen[i], sizeof seen[i], "%s", c.ipod_path);
        for (int j = 0; j < i; j++) assert(strcmp(seen[i], seen[j]) != 0);
        assert(strcmp(seen[i], r.ipod_path) != 0);
    }
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/iPod_Control/Music/F%02d", root, i);
        assert(count_suffix(p, ".part") == 0);
    }

    // --- failures ---
    assert(copier_copy_track(root, "/no/such/file.mp3", &r) == COPIER_ERR_IO);
    assert(copier_copy_track(root, "/tmp/noextension", &r) == COPIER_ERR_ARG);
    assert(copier_copy_track(root, "/tmp/bad.m p3", &r) == COPIER_ERR_ARG);
    assert(copier_copy_track("/tmp/definitely_not_an_ipod", src, &r) == COPIER_ERR_NOFOLDER);
    assert(copier_copy_track(NULL, src, &r) == COPIER_ERR_NULL);
    assert(copier_copy_track(root, src, NULL) == COPIER_ERR_NULL);
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/iPod_Control/Music/F%02d", root, i);
        assert(count_suffix(p, ".part") == 0);                                 // failures leave nothing behind
    }

    // --- remove: works once, then refuses anything outside iPod_Control/Music ---
    copy_result_t g;
    assert(copier_copy_track(root, src, &g) == COPIER_OK);
    struct stat st;
    assert(stat(g.full_path, &st) == 0);
    assert(copier_remove_track(root, g.ipod_path) == COPIER_OK);
    assert(stat(g.full_path, &st) != 0);
    assert(copier_remove_track(root, g.ipod_path) == COPIER_ERR_IO);           // already gone
    assert(copier_remove_track(root, ":iPod_Control:Music:..:..:x.mp3") == COPIER_ERR_ARG);
    assert(copier_remove_track(root, ":iPod_Control:Music:F00:../../x.mp3") == COPIER_ERR_ARG);
    assert(copier_remove_track(root, ":other:F00:ABCD.mp3") == COPIER_ERR_ARG);
    assert(copier_remove_track(root, "/etc/passwd") == COPIER_ERR_ARG);
    assert(copier_remove_track(NULL, g.ipod_path) == COPIER_ERR_NULL);

    free(data);
    remove(src);
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    system(cmd);
    puts("copier tests passed");
    return 0;
}
