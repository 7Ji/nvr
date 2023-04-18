#ifndef __HAVE_CAMERA_H
#define __HAVE_CAMERA_H

#include "common.h"

#include <linux/limits.h>
#include <pthread.h>

#include "storage.h"

struct camera {
    struct camera *next_camera;
    char name[NAME_MAX];
    unsigned short len_name;
    char strftime[NAME_MAX];
    char url[PATH_MAX];
    char path[PATH_MAX];
    char *subpath;
    size_t len_subpath_max;
    // struct storage const *storage;
    bool recorder_working_this;
    bool recorder_working_last;
    pthread_t recorder_thread_this;
    pthread_t recorder_thread_last;
    unsigned breaks;
    bool break_waiting;
    unsigned break_wait_ticks;
};

struct camera *parse_argument_camera(char const *arg);

int cameras_init(struct camera *camera_head, struct storage const *storage_head);

int cameras_work(struct camera *camera_head);

#endif