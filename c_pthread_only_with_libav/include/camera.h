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
    struct storage const *storage;
    pthread_t recorder_pthread;
};

struct camera *parse_argument_camera(char const *arg);

int cameras_init(struct camera *camera_head, struct storage const *storage_head);

int cameras_start(struct camera *camera_head);

#endif