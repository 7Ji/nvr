#include "common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include "print.h"
#include "version.h"
#include "storage.h"
#include "camera.h"
#include "mkdir.h"
#include "help.h"

int wait_all(struct storage *const storage_head, struct camera *const camera_head) {
    while (true) {
        if (storages_clean(storage_head)) {
            pr_error("Storages cleaner breaks\n");
            return 1;
        }
        if (cameras_work(camera_head)) {
            pr_error("Cameras worker breaks\n");
            return 2;
        }
        sleep(1);
    }
    return 0;
}

int unbuffer() {
    if (setvbuf(stdout, NULL, _IOLBF, BUFSIZ)) {
        pr_error_with_errno("Failed to unbuffer stdout");
        return 1;
    }
    if (setvbuf(stderr, NULL, _IOLBF, BUFSIZ)) {
        pr_error_with_errno("Failed to unbuffer stderr");
        return 2;
    }
    return 0;
}

int main(int const argc, char const *const argv[]) {
    if (unbuffer()) {
        return -1;
    }
    if (argc < 2) {
        pr_error("Arguments too few\n");
        puts(help);
        return 1;
    }
    struct camera *camera_head = NULL;
    struct camera *camera_last = NULL;
    struct storage *storage_head = NULL;
    struct storage *storage_last = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2]) {
            char const *const arg = argv[i] + 2;
            if (!strncmp(arg, "help", 5)) {
                puts(help);
                return 0;
            }
            if (!strncmp(arg, "version", 8)) {
                puts(version);
                return 0;
            }
            if (!strncmp(arg, "limit-move-across-fs", 21)) {
                if (storage_limit_move_across_fs()) {
                    pr_error("Failed to limit move across fs\n");
                    return 2;
                }
                continue;
            }
            if (++i == argc) {
                pr_error("Illegal argument, needs suffix argument: '%s'\n", argv[i - 1]);
                return 2;
            }
            if (!strncmp(arg, "camera", 7)) {
                struct camera *const camera_current = parse_argument_camera(argv[i]);
                if (!camera_current) {
                    pr_error("Failed to parse camera argument: '%s'\n", argv[i]);
                    return 3;
                }
                if (!camera_head) {
                    camera_head = camera_current;
                }
                if (camera_last) {
                    camera_last->next_camera = camera_current;
                }
                camera_last = camera_current;
            } else if (!strncmp(arg, "storage", 8)) {
                struct storage *const storage_current = parse_argument_storage(argv[i]);
                if (!storage_current) {
                    pr_error("Failed to parse camera argument: '%s'\n", argv[i]);
                    return 4;
                }
                if (!storage_head) {
                    storage_head = storage_current;
                }
                if (storage_last) {
                    storage_last->next_storage = storage_current;
                }
                storage_last = storage_current;
            } else if (!strncmp(arg, "max-cleaners", 13)) {
                storage_parse_max_cleaners(argv[i]);
            } else {
                pr_error("Illegal argument, unrecognized --argument: '%s'\n", argv[i - 1]);
                return 5;
            }
        } else {
            pr_error("Illegal argument, unrecognized: '%s'\n", argv[i]);
            puts(help);
            return 6;
        }
    }
    if (!camera_head || !camera_last) {
        pr_error("No camera defined\n");
        puts(help);
        return 7;
    }
    if (!storage_head || !storage_last) {
        pr_error("No storage defined\n");
        puts(help);
        return 8;
    }
    if (storages_init(storage_head)) {
        pr_error("Failed to init storages\n");
        return 9;
    }
    if (cameras_init(camera_head, storage_head)) {
        pr_error("Failed to init cameras\n");
        return 10;
    }
    if (wait_all(storage_head, camera_head)) {
        pr_error("Bad things happended when we working on storage and cameras\n");
        return 11;
    }
    return 0;
}