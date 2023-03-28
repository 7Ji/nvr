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
        for (struct storage *storage_current = storage_head; storage_current; storage_current = storage_current->next_storage) {
            long ret;
            int r = pthread_tryjoin_np(storage_current->watcher_pthread, (void **)&ret);
            switch (r) {
            case EBUSY:
                sleep(1);
                break;
            case 0:
                pr_error("Thread for storage watcher of '%s' exited (with %ld) which is not expected\n", storage_current->path, ret);
                exit(EXIT_FAILURE);
            default:
                pr_error("Unpected return from pthread_tryjoin_np: %d\n", r);
                return 1;
            }
        }
        for (struct camera *camera_current = camera_head; camera_current; camera_current = camera_current->next_camera) {
            long ret;
            int r = pthread_tryjoin_np(camera_current->recorder_pthread, (void **)&ret);
            switch (r) {
            case EBUSY:
                sleep(1);
                break;
            case 0:
                pr_error("Thread for camera recorder of '%s' exited (with %ld) which is not expected\n", camera_current->url, ret);
                exit(EXIT_FAILURE);
            default:
                pr_error("Unpected return from pthread_tryjoin_np: %d\n", r);
                return 1;
            }
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
    if (storages_start(storage_head)) {
        pr_error("Failed to start storage watchers\n");
        return 11;
    }
    if (cameras_start(camera_head)) {
        pr_error("Failed to start camera recorders\n");
        return 12;
    }
    if (wait_all(storage_head, camera_head)) {
        pr_error("Child thread exited\n");
        return 13;
    }
    return 0;
}