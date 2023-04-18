#ifndef __HAVE_STORAGE_H
#define __HAVE_STORAGE_H

#include "common.h"

#include <stdbool.h>
#include <linux/limits.h>
#include <sys/statvfs.h>
#include <pthread.h>
#include <dirent.h>


enum storage_threshold_type {
    STORAGE_THRESHOLD_TYPE_PERCENT,
    STORAGE_THRESHOLD_TYPE_SIZE,
    STORAGE_THRESHOLD_TYPE_BLOCK
};

struct storage_threshold {
    enum storage_threshold_type type;
    size_t value;
    fsblkcnt_t free_blocks;
};

struct storage_thresholds {
    struct storage_threshold from, to;
};

struct storage {
    struct storage *next_storage;
    char path[PATH_MAX];
    unsigned short len_path;
    struct storage_thresholds thresholds;
    bool cleaning;
    pthread_t cleaner_thread;
    bool half_duplex;
    pthread_mutex_t io_mutex;
    bool io_mutex_need_lock_this;
    pthread_mutex_t *next_io_mutex;
    bool io_mutex_need_lock_next;
    bool io_mutex_need_lock;
    DIR *dir;
    char path_oldest[PATH_MAX];
    char *subpath_oldest;
    char path_new[PATH_MAX];
    char *subpath_new;
    size_t len_path_new_allow;
    bool move_to_next;
};

void storage_parse_max_cleaners(char const *const arg);

struct storage *parse_argument_storage(char const *arg);

int storages_init(struct storage *storage_head);

int storages_clean(struct storage *storage_head);

#endif