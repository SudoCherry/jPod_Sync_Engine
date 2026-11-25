#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "device.h"
#include "library.h"
#include "database.h"
#include "config.h"

// Main User UI Function
void handle_ui(void);

int main(void) {
    handle_ui();
    return 0;
}

// Print a prompt and read one line (without the newline). Returns -1 on end of input.
static int read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    if (fgets(buf, (int)size, stdin) == NULL) return -1;
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

// Tidy up a path the user typed or pasted, in place:
// trims spaces, removes quotes ('/my/path'), removes drag-and-drop backslashes
// (My\ Music), and expands a leading "~" to the home folder.
static void clean_path(char *path, size_t size) {
    // leading and trailing spaces
    size_t lead = strspn(path, " ");
    if (lead > 0) memmove(path, path + lead, strlen(path + lead) + 1);
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == ' ') path[--len] = '\0';

    // matching quotes around the whole path
    if (len >= 2 && (path[0] == '\'' || path[0] == '"') && path[len - 1] == path[0]) {
        memmove(path, path + 1, len - 2);
        path[len - 2] = '\0';
    }

    // backslash escapes: drop the backslash, keep the character after it
    char *w = path;
    for (char *r = path; *r; r++) {
        if (*r == '\\' && r[1] != '\0') r++;
        *w++ = *r;
    }
    *w = '\0';

    // the shell expands "~" for you, but this program has to do it itself
    const char *home = getenv("HOME");
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0') && home != NULL) {
        char tmp[CONFIG_PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%s%s", home, path + 1);
        snprintf(path, size, "%s", tmp);
    }
}

// Throw away the old library and scan `folder` fresh, so scanning twice never
// lists the same song twice. Returns a LIBRARY_* code.
static int scan_folder(library_t *lib, const char *folder) {
    library_free(lib);
    library_init(lib);

    errno = 0;
    int rc = library_scan(lib, folder);

    if (rc == LIBRARY_ERR_FOLDER) {
        printf("Couldn't open \"%s\": %s\n", folder, strerror(errno));
    } else if (rc != LIBRARY_OK) {
        printf("Scan failed (error %d).\n", rc);
    } else {
        printf("Found %zu %s in %s.\n", lib->count, lib->count == 1 ? "track" : "tracks", folder);
        for (size_t i = 0; i < lib->count && i < 5; i++) {
            const track_t *t = &lib->tracks[i];
            if (t->artist[0] != '\0') printf("  %s - %s", t->artist, t->title);
            else                      printf("  %s", t->title);
            if (t->album[0] != '\0') printf("  [%s]", t->album);
            printf("\n");
        }
    }

    if (rc != LIBRARY_OK) {          // never keep a half-finished scan
        library_free(lib);
        library_init(lib);
    }
    return rc;
}

void handle_ui(void) {
    char input[100];
    ipod_device_t ipod;
    library_t library;
    database_t ipod_db;
    config_t config;

    device_init(&ipod);   // start with a clean, disconnected device
    library_init(&library);
    database_init(&ipod_db);
    config_load(&config); // first run: no settings file yet, config stays empty

    while (1) {
        if (config.music_folder[0] != '\0') {
            printf("\nMusic folder: %s\n", config.music_folder);
        } else {
            printf("\nMusic folder: (not set - press M)\n");
        }
        printf("What Operation Would You Like To Perform?\n");
        printf("(H)elp, (M)usic folder, (S)ync over IEEE1394, S(Y)nc over USB, (A)dd tracks, "
               "(R)emove tracks, (Q)uit Programme:\n");

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0') {
            continue;
        }

        switch (tolower((unsigned char)input[0])) {
            case 'h':
                printf("Music folder or (M): choose the root folder of your music. It is saved, so you only do this once.\n");
                printf("Sync over IEEE1394 or (S): sync older FireWire iPods.\n");
                printf("Sync over USB or (Y): sync newer USB iPods.\n");
                printf("Add tracks or (A): adds all music from your music folder to the iPod and updates the database.\n");
                printf("Remove tracks or (R): removes selected music based off the database on the iPod, then reupdates the database.\n");
                printf("Quit Programme or (Q): simply quits the program.\n");
                printf("Debug: (C) simulate plugging in an iPod, (D) simulate unplugging it,\n");
                printf("       (L) list the tracks in an iTunesDB file,\n");
                printf("       (I) inspect the structure of an iTunesDB file.\n");
                break;

            case 'm': {
                char folder[CONFIG_PATH_MAX];
                if (read_line("Path to your music folder: ", folder, sizeof(folder)) != 0) break;
                clean_path(folder, sizeof(folder));
                if (folder[0] == '\0') {
                    printf("No folder entered, keeping the current setting.\n");
                    break;
                }

                // Only remember a folder that actually opens
                if (scan_folder(&library, folder) != LIBRARY_OK) {
                    printf("Music folder not changed.\n");
                    break;
                }
                snprintf(config.music_folder, sizeof(config.music_folder), "%s", folder);
                if (config_save(&config) == CONFIG_OK) {
                    printf("Music folder saved.\n");
                } else {
                    printf("Music folder set, but it couldn't be saved, so it will be forgotten when you quit.\n");
                }
                break;
            }

            case 's':
            case 'y':
            case 'r':
                // All of these need a ready iPod before they can do anything
                if (!device_is_ready(&ipod)) {
                    printf("No iPod ready. Connect one first.\n");
                } else {
                    printf("(not implemented yet)\n");
                }
                break;

            case 'a':
                // Scanning only reads the computer, so no iPod is needed yet
                if (config.music_folder[0] == '\0') {
                    printf("No music folder set yet. Press M to choose one.\n");
                } else {
                    if (scan_folder(&library, config.music_folder) == LIBRARY_OK) {
                        printf("(Scan only - copying tracks to the iPod isn't built yet.)\n");
                    }
                }
                break;

            // ---- temporary debug commands until real detection exists ----
            case 'c':
                if (device_connect(&ipod) != DEVICE_OK) {
                    printf("Already connected.\n");
                    break;
                }
                device_identify(&ipod, 1, 2004, 4, 40, IPOD_USB, "FAKE-SERIAL-001");
                printf("Fake iPod connected (serial %s).\n", ipod.serial_number);
                break;

            case 'l': {
                char dbpath[CONFIG_PATH_MAX];
                if (read_line("Path to an iTunesDB file: ", dbpath, sizeof(dbpath)) != 0) break;
                clean_path(dbpath, sizeof(dbpath));

                int rc = database_load(&ipod_db, dbpath);
                if (rc == DATABASE_ERR_IO) {
                    printf("Couldn't read that file.\n");
                } else if (rc != DATABASE_OK) {
                    printf("That doesn't look like a valid iTunesDB (error %d).\n", rc);
                } else {
                    printf("%zu tracks in the database.\n", ipod_db.count);
                    for (size_t i = 0; i < ipod_db.count && i < 10; i++) {
                        printf("  [%u] %s - %s\n", (unsigned)ipod_db.tracks[i].id,
                               ipod_db.tracks[i].artist, ipod_db.tracks[i].title);
                    }
                }
                break;
            }

            case 'i': {
                char ipath[CONFIG_PATH_MAX];
                if (read_line("Path to an iTunesDB file: ", ipath, sizeof(ipath)) != 0) break;
                clean_path(ipath, sizeof(ipath));

                int rc = database_inspect(ipath, stdout);
                if (rc == DATABASE_ERR_IO) {
                    printf("Couldn't read that file.\n");
                } else if (rc != DATABASE_OK) {
                    printf("That doesn't look like a valid iTunesDB (error %d).\n", rc);
                }
                break;
            }

            case 'd':
                device_disconnect(&ipod);
                printf("iPod disconnected.\n");
                break;

            case 'q':
                library_free(&library);
                database_free(&ipod_db);
                return;

            default:
                printf("Invalid option.\n");
                break;
        }
    }

    library_free(&library);   // reached on end of input
    database_free(&ipod_db);
}
