#include "camera.h"

#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include "print.h"
#include "argsep.h"
#include "mux.h"
#include "mkdir.h"

static struct storage const *storage;
static time_t time_next = 0;
static struct tm tms_now;

struct camera *parse_argument_camera(char const *const arg) {
    pr_debug("Parsing camera definition: '%s'\n", arg);
    char const *seps[2];
    char const *end = NULL;
    unsigned short sep_id = parse_argument_seps(arg, seps, 2, &end);
    if (sep_id < 2) {
        pr_error("Camera definition incomplete: '%s'\n", arg);
        return NULL;
    }
    if (!end) {
        pr_error("Camera definition not finished properly: '%s'\n", arg);
        return NULL;
    }
    unsigned short len_name = seps[0] - arg;
    if (len_name > NAME_MAX) {
        pr_error("Name in camera definition too long: '%s'\n", arg);
        return NULL;
    }
    unsigned short len_strftime = seps[1] - seps[0] - 1;
    if (len_strftime > NAME_MAX) {
        pr_error("strftime in camera definition too long: '%s'\n", arg);
        return NULL;
    }
    if (!len_strftime && !len_name) {
        pr_error("Both strftime and name not defined in camera definition: '%s'\n", arg);
        return NULL;
    }
    unsigned short len_url = end - seps[1] - 1;
    if (len_url > PATH_MAX) {
        pr_error("URL in camera definition too long: '%s'\n", arg);
        return NULL;
    }
    if (!len_url) {
        pr_error("URL not defined in camera deifnition: '%s'\n", arg);
        return NULL;
    }
    struct camera *camera = malloc(sizeof *camera);
    if (!camera) {
        pr_error_with_errno("Failed to allocate memory for camera");
        return NULL;
    }
    strncpy(camera->name, arg, len_name);
    camera->name[len_name] = '\0';
    if (len_strftime) {
        strncpy(camera->strftime, seps[0] + 1, len_strftime);
        camera->strftime[len_strftime] = '\0';
    } else {
        if (snprintf(camera->strftime, sizeof camera->strftime, "%s_%s", camera->name, "%Y%m%d_%H%M%S") < 0) {
            pr_error_with_errno("Failed to generate strftime");
            free(camera);
            return NULL;
        }
        pr_warn("Generated strftime '%s' from name '%s' since it's not set in camera definition '%s'\n", camera->strftime, camera->name, arg);
    }
    strncpy(camera->url, seps[1] + 1, len_url);
    camera->url[len_url] = '\0';
    camera->len_name = len_name;
    camera->next_camera = NULL;
    camera->recorder_working_this = false;
    camera->recorder_working_last = false;
    camera->breaks = 0;
    camera->break_waiting = false;
    pr_debug("Camera defitnition: name: '%s', strftime: '%s', url: '%s'\n", camera->name, camera->strftime, camera->url);
    return camera;
}

int cameras_init(struct camera *const camera_head, struct storage const *const storage_use) {
    storage = storage_use;
    for (struct camera *camera = camera_head; camera; camera = camera->next_camera) {
        strncpy(camera->path, storage->path, storage->len_path);
        camera->path[storage->len_path] = '/';
        camera->subpath = camera->path + storage->len_path + 1;
        camera->len_subpath_max = PATH_MAX - storage->len_path - 1;
    }
    return 0;
}

static int camera_record(struct camera *const camera) {
    size_t len = strftime(camera->subpath, camera->len_subpath_max, camera->strftime, &tms_now);
    if (!len) {
        pr_error_with_errno("Failed to create strftime file name");
        return 1;
    }
    char *const suffix = camera->subpath + len;
    strncpy(suffix, ".mkv", 5);
    if (mkdir_recursive_only_parent(camera->path, 0755)) {
        pr_error("Failed to mkdir for all parents for '%s'\n", camera->path);
        return 2;
    }
    pr_warn("Recording from '%s' to '%s', duration %lds, thread %lx\n", camera->url, camera->path, time_next - time(NULL), pthread_self());
    if (mux(camera->url, camera->path, time_next + 5)) {
        pr_error("Failed to record from '%s' to '%s' (path might be reused and changed), thread %lx\n", camera->url, camera->path, pthread_self());
        return 3;
    }
    pr_warn("Recording ended from '%s' to '%s' (path could've changed as we reuse memory)\n", camera->url, camera->path);
    return 0;
}

static void *camera_record_thread(void *arg) {
    long r = camera_record((struct camera *)(arg));
    return (void *)r;
}

static int camera_push_this_to_last(struct camera *camera) {
    if (camera->recorder_working_this) {
        if (camera->recorder_working_last) {
            if (pthread_kill(camera->recorder_thread_last, SIGINT)) {
                pr_error("Faile to send kill signal to last record thread for camera of url '%s'\n", camera->url);
                return 1;
            }
            int r;
            long ret;
            switch ((r = pthread_tryjoin_np(camera->recorder_thread_last, (void **)&ret))) {
            case EBUSY:
                pr_error("Failed to kill pthread for last record thread for camera of url '%s' for good\n", camera->url);
                return 2;
            case 0:
                if (ret) {
                    pr_error("Thread for killed recorder of camera of url '%s' breaks with %ld\n", camera->url, ret);
                    ++camera->breaks;
                    // return 3;
                } else {
                    pr_warn("Last camera recorder for url '%s' safely ends\n", camera->url);
                    camera->breaks = 0;
                }
                break;
            default:
                pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
                return -1;
            }
        }
        camera->recorder_thread_last = camera->recorder_thread_this;
        camera->recorder_working_last = true;
        camera->recorder_working_this = false;
    }
    return 0;
}

static int camera_create_thread(struct camera *const camera) {
    if (camera->break_waiting) {
        if (--camera->break_wait_ticks) {
            return 0;
        } else {
            /* if wait ticks is 0, it can enter the following logic */
            camera->break_waiting = false;
        }
    } else if (camera->breaks > 100) {
        if (camera->breaks > 10000) {
            camera->break_wait_ticks = 600;
        } else if (camera->breaks > 1000)  {
            camera->break_wait_ticks = 90;
        } else {
            camera->break_wait_ticks = 10;
        }
        camera->break_waiting = true;
        return 0;
    }
    if (pthread_create(&camera->recorder_thread_this, NULL, camera_record_thread, (void *)camera)) {
        pr_error("Failed to create thread to record camera for url '%s'\n", camera->url);
        return 1;
    }
    camera->recorder_working_this = true;
    return 0;
}

static int camera_make_sure_working(struct camera *const camera) {
    if (camera->recorder_working_this) {
        int r;
        long ret;
        switch ((r = pthread_tryjoin_np(camera->recorder_thread_this, (void **)&ret))) {
        case EBUSY:
            break;
        case 0:
            if (ret) {
                pr_error("Camera recorder for url '%s' breaks with return value '%ld'\n", camera->url, ret);
                ++camera->breaks;
            } else {
                pr_warn("Camera recorder for url '%s' safely ends\n", camera->url);
                camera->breaks = 0;
            }
            camera->recorder_working_this = false;
            break;
        default:
            pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
            return -1;
        }
    }
    if (!camera->recorder_working_this) { /* It must be at least recording for 'this' */
        if (camera_create_thread(camera)) {
            pr_error("Failed to create thread for camera of url '%s'\n", camera->url);
            return 1;
        }
    }
    return 0;
}

static int camera_check_last(struct camera *const camera) {
    if (camera->recorder_working_last) {
        int r;
        long ret;
        switch ((r = pthread_tryjoin_np(camera->recorder_thread_last, (void **)&ret))) {
        case EBUSY:
            break;
        case 0:
            if (ret) {
                pr_error("Last camera recorder for url '%s' breaks with return value '%ld'\n", camera->url, ret);
                ++camera->breaks;
            } else {
                pr_warn("Last camera recorder for url '%s' safely ends\n", camera->url);
                camera->breaks = 0;
            }
            camera->recorder_working_last = false;
            break;
        default:
            pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
            return -1;
        }
    }
    return 0;
}

int cameras_work(struct camera *const camera_head) {
    time_t time_now = time(NULL);
    if (time_now >= time_next) {
        localtime_r(&time_now, &tms_now);
        struct tm tms_next = tms_now;
        int minute = (tms_now.tm_min + 11) / 10 * 10;
        if (minute >= 60) {
            tms_next.tm_min = minute % 60;
            ++tms_next.tm_hour;
        } else {
            tms_next.tm_min = minute;
        }
        tms_next.tm_sec = 0;
        time_next = mktime(&tms_next);
        for (struct camera *camera = camera_head; camera; camera = camera->next_camera) {
            if (camera_push_this_to_last(camera)) {
                pr_error("Failed to push this to last for camera of url '%s'\n", camera->url);
                return 1;
            }
            if (camera_create_thread(camera)) {
                pr_error("Failed to create thread for camera of url '%s'\n", camera->url);
                return 2;
            }
        }
    }
    for (struct camera *camera = camera_head; camera; camera = camera->next_camera) {
        if (camera_make_sure_working(camera)) {
            pr_error("Failed to make sure camera for url '%s' is working\n", camera->url);
            return 3;
        }
        if (camera_check_last(camera)) {
            pr_error("Failed to check last camera for url '%s'\n", camera->url);
            return 3;
        }
    }
    return 0;
}