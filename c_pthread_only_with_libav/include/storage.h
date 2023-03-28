#ifndef __HAVE_STORAGE_H
#define __HAVE_STORAGE_H

#include "common.h"

#include <linux/limits.h>
#include <sys/statvfs.h>
#include <pthread.h>
#include <dirent.h>

struct threshold {
    unsigned short from_free_percent;
    unsigned short to_free_percent;
};

struct space {
    fsblkcnt_t from_free_blocks;
    fsblkcnt_t to_free_blocks;
};

struct storage {
    struct storage *next_storage;
    char path[PATH_MAX];
    unsigned short len_path;
    struct threshold threshold;
    struct space space;
    pthread_t watcher_pthread;
    DIR *dir;
};

struct storage *parse_argument_storage(char const *arg);

int storages_init(struct storage *storage_head);

int storages_start(struct storage *storage_head);

#endif