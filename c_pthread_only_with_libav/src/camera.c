#include "camera.h"

#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include "print.h"
#include "argsep.h"
#include "mux.h"
#include "mkdir.h"

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
    pr_debug("Camera defitnition: name: '%s', strftime: '%s', url: '%s'\n", camera->name, camera->strftime, camera->url);
    return camera;
}

int cameras_init(struct camera *const camera_head, struct storage const *const storage_head) {
    for (struct camera *camera_current = camera_head; camera_current; camera_current = camera_current->next_camera) {
        camera_current->storage = storage_head;
    }
    return 0;
}

struct mux_thread_arg {
    char const *in;
    char const *out;
    unsigned int duration;
};

static void *mux_thread(void *arg) {
    struct mux_thread_arg *mux_thread_arg = arg;
    long ret = mux(mux_thread_arg->in, mux_thread_arg->out, mux_thread_arg->duration);
    return (void *)ret;
};

static int camera_recorder(struct camera const *const camera) {
    char path[PATH_MAX];
    strncpy(path, camera->storage->path, camera->storage->len_path);
    path[camera->storage->len_path] = '/';
    char *const subpath = path + camera->storage->len_path + 1;
    size_t const len_subpath_max = PATH_MAX - camera->storage->len_path - 1;
    pthread_t thread_mux_last;
    bool thread_mux_running_this = false;
    bool thread_mux_running_last = false;
    while (true) {
        time_t time_now = time(NULL);
        struct tm tms_now;
        localtime_r(&time_now, &tms_now);
        size_t len = strftime(subpath, len_subpath_max, camera->strftime, &tms_now);
        if (!len) {
            pr_error_with_errno("Failed to create strftime file name");
            return 1;
        }
        char *const suffix = subpath + len;
        strncpy(suffix, ".mkv", 5);
        if (mkdir_recursive_only_parent(path, 0755)) {
            pr_error("Failed to mkdir for all parents for '%s'\n", path);
            return 1;
        }
        struct tm tms_future = tms_now;
        int minute = (tms_now.tm_min + 11) / 10 * 10;
        if (minute >= 60) {
            tms_future.tm_min = minute - 60;
            ++tms_future.tm_hour;
        } else {
            tms_future.tm_min = minute;
        }
        tms_future.tm_sec = 0;
        time_t time_future = mktime(&tms_future);
        time_t time_diff = time_future - time_now;
        struct mux_thread_arg mux_thread_arg = {
            .in = camera->url,
            .out = path,
            .duration = time_diff + 3
        };
        pr_warn("Recording from '%s' to '%s', duration %us\n", camera->url, path, mux_thread_arg.duration);
        pthread_t thread_mux_this;
        if (pthread_create(&thread_mux_this, NULL, mux_thread, (void *)&mux_thread_arg)) {
            pr_error_with_errno("Faile to create pthread to record camera of url '%s'", camera->url);
            return 2;
        }
        thread_mux_running_this = true;
        while (time_now < time_future && thread_mux_running_this) {
            long ret;
            int r = pthread_tryjoin_np(thread_mux_this, (void **)&ret);
            switch (r) {
            case EBUSY:
                break;
            case 0:
                if (ret) {
                    pr_error("Thread for remuxer of camera of url '%s' exited (with %ld) which is not expected, but we accept it\n", camera->url, ret);
                }
                thread_mux_running_this = false;
                break;
            default:
                pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
                return 1;
            }
            if (thread_mux_running_last) {
                switch ((r = pthread_tryjoin_np(thread_mux_last, (void **)&ret))) {
                case EBUSY:
                    break;
                case 0:
                    if (ret) {
                        pr_error("Thread for remuxer of camera of url '%s' exited (with %ld) which is not expected, but we accept it\n", camera->url, ret);
                    }
                    thread_mux_running_last = false;
                    break;
                default:
                    pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
                    return 1;
                }
            }
            time_diff = time_future - (time_now = time(NULL));
            sleep(time_diff > 10 ? 10 : time_diff);
        }
        if (thread_mux_running_this) {
            if (thread_mux_running_last) {
                long ret;
                int r = pthread_tryjoin_np(thread_mux_last, (void **)&ret);
                switch (r) {
                case EBUSY:
                    if (pthread_kill(thread_mux_last, SIGINT)) {
                        pr_error("Faile to send kill signal to last remux thread for camera of url '%s'", camera->url);
                        return 1;
                    }
                    switch ((r = pthread_tryjoin_np(thread_mux_last, (void **)&ret))) {
                    case EBUSY:
                        pr_error("Failed to kill pthread for last remux thread for camera of url '%s' for good", camera->url);
                        return 1;
                    case 0:
                        if (ret) {
                            pr_error("Thread for remuxer of camera of url '%s' exited (with %ld) which is not expected, but we accept it\n", camera->url, ret);
                        }
                        thread_mux_running_last = false;
                        break;
                    default:
                        pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
                        return 1;
                    }
                    break;
                case 0:
                    if (ret) {
                        pr_error("Thread for remuxer of camera of url '%s' exited (with %ld) which is not expected, but we accept it\n", camera->url, ret);
                    }
                    thread_mux_running_last = false;
                    break;
                default:
                    pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
                    return 1;
                }
            }
            thread_mux_last = thread_mux_this;
            thread_mux_running_last = true;
        }
    }
    return 0;
}

static void *camera_record_thread(void *arg) {
    long r = camera_recorder((struct camera *)(arg));
    return (void *)r;
}

int cameras_start(struct camera *const camera_head) {
    for (struct camera *camera_current = camera_head; camera_current; camera_current = camera_current->next_camera) {
        if (pthread_create(&camera_current->recorder_pthread, NULL, camera_record_thread, (void *)camera_current)) {
            pr_error("Failed to create pthread for camera_recorder for '%s'\n", camera_current->url);
            return 1;
        }
    }
    return 0;
}