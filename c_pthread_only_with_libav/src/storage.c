#include "storage.h"

#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

#include "print.h"
#include "argsep.h"
#include "mkdir.h"

static unsigned max_cleaners = 0;
static unsigned running_cleaners = 0;
bool storage_oneshot_cleaner = false;

char const storage_threshold_type_strings[][8] = {
    "percent",
    "size",
    "block"
};

void storage_parse_max_cleaners(char const *const arg) {
    long cleaners = strtol(arg, NULL, 10);
    if (cleaners > 0) {
        max_cleaners = cleaners;
        storage_oneshot_cleaner = true;
    } else {
        max_cleaners = 0;
        storage_oneshot_cleaner = false;
    }
    pr_warn("Limited max concurrent cleaners to %u, do note these cleaners will be one-shot only and the cleaner end trigger might not work as intended\n", max_cleaners);
}

static enum storage_threshold_type parse_storage_thresholds(char const *const arg, size_t *const value) {
    char *suffix;
    *value = strtoul(arg, &suffix, 10);
    switch (*suffix) {
    case 'T':
    case 't':
        *value *= 0x400;
        __attribute__((fallthrough));
    case 'G':
    case 'g':
        *value *= 0x400;
        __attribute__((fallthrough));
    case 'M':
    case 'm':
        *value *= 0x400;
        __attribute__((fallthrough));
    case 'K':
    case 'k':
        *value *= 0x400;
        __attribute__((fallthrough));
    case 'B':
    case 'b':
        return STORAGE_THRESHOLD_TYPE_SIZE;
    case '%':
        return STORAGE_THRESHOLD_TYPE_PERCENT;
    default:
        return STORAGE_THRESHOLD_TYPE_BLOCK;
    }
    
}

struct storage *parse_argument_storage(char const *const arg) {
    pr_debug("Parsing storage definition: '%s'\n", arg);
    char const *seps[3];
    char const *end = NULL;
    unsigned short sep_id = parse_argument_seps(arg, seps, 3, &end);
    if (sep_id < 2) {
        pr_error("Storage definition incomplete: '%s'\n", arg);
        return NULL;
    }
    if (!end) {
        pr_error("Storage definition not finished properly: '%s'\n", arg);
        return NULL;
    }
    unsigned short len_path = seps[0] - arg;
    if (len_path > PATH_MAX) {
        pr_error("Path in storage definition too long: '%s'\n", arg);
        return NULL;
    }
    size_t threshold_from_value;
    enum storage_threshold_type threshold_from_type = parse_storage_thresholds(seps[0] + 1, &threshold_from_value);
    size_t threshold_to_value;
    enum storage_threshold_type threshold_to_type = parse_storage_thresholds(seps[1] + 1, &threshold_to_value);
    bool half_duplex = false;
    if (sep_id > 2) {
        if (!strncmp(seps[2] + 1, "half_duplex", 12)) {
            pr_warn("Storage is half-duplex: '%s', only one of read and write will be performed on it at the same time\n", arg);
            half_duplex = true;
        }
    }
    struct storage *storage = malloc(sizeof *storage);
    if (!storage) {
        pr_error_with_errno("Failed to allocate memory for storage");
        return NULL;
    }
    strncpy(storage->path, arg, len_path);
    storage->path[len_path] = '\0';
    storage->len_path = len_path;
    storage->thresholds.from.value = threshold_from_value;
    storage->thresholds.from.type = threshold_from_type;
    storage->thresholds.to.value = threshold_to_value;
    storage->thresholds.to.type = threshold_to_type;
    storage->half_duplex = half_duplex;
    storage->io_mutex_need_lock = half_duplex;
    storage->io_mutex_need_lock_this = half_duplex;
    storage->io_mutex_need_lock_next = false;
    storage->next_storage = NULL;
    pr_warn("Storage defitnition: path: '%s' (length %hu), clean from %lu (%s), to %lu (%s)\n", storage->path, storage->len_path, storage->thresholds.from.value, storage_threshold_type_strings[storage->thresholds.from.type], storage->thresholds.to.value, storage_threshold_type_strings[storage->thresholds.to.type]);
    return storage;
}

static void storage_init_thresholds(struct storage_threshold *const threshold, struct statvfs const *const st) {
    switch (threshold->type) {
    case STORAGE_THRESHOLD_TYPE_PERCENT:
        threshold->free_blocks = st->f_blocks * threshold->value / 100;
        break;
    case STORAGE_THRESHOLD_TYPE_BLOCK:
        threshold->free_blocks = threshold->value;
        break;
    case STORAGE_THRESHOLD_TYPE_SIZE:
        threshold->free_blocks = threshold->value / st->f_frsize;
        break;
    }
    if (threshold->free_blocks > st->f_blocks) {
        threshold->free_blocks = st->f_blocks;
    }
}

static int storage_init(struct storage *const storage) {
    if (mkdir_recursive(storage->path, 0755)) {
        pr_error("Failed to make sure storage structure for '%s' exsits", storage->path);
    }
    struct statvfs st;
    if (statvfs(storage->path, &st) < 0) {
        pr_error_with_errno("Failed to get vfs stat for '%s'", storage->path);
        return 1;
    }
    if (!st.f_blocks) {
        pr_error("Storage '%s' has 0 blocks\n", storage->path);
        return 2;
    }
    storage_init_thresholds(&storage->thresholds.from, &st);
    storage_init_thresholds(&storage->thresholds.to, &st);
    pr_warn("Thresholds on storage '%s': from %lu free blocks to %lu free blocks, each block size %lu\n", storage->path, storage->thresholds.from.free_blocks, storage->thresholds.to.free_blocks, st.f_frsize);
    if (!(storage->dir = opendir(storage->path))) {
        pr_error_with_errno("Failed to open storage '%s'", storage->path);
        return 4;
    }
    strncpy(storage->path_oldest, storage->path, storage->len_path + 1);
    storage->subpath_oldest = storage->path_oldest + storage->len_path;
    if (*storage->subpath_oldest) {
        pr_error("Path of storage '%s' not properly ended or length %hu is not right\n", storage->path, storage->len_path);
        return 5;
    }
    if (storage->half_duplex) {
        // storage->io_mutex_need_lock_this = true;
        // storage->io_mutex_need_lock = true;
        if (pthread_mutex_init(&storage->io_mutex, NULL)) {
            pr_error("Failed to init io mutex for storage '%s'\n", storage->path);
            return 6;
        }
    }
    if ((storage->move_to_next = storage->next_storage)) {
        strncpy(storage->path_new, storage->next_storage->path, storage->next_storage->len_path);
        storage->subpath_new = storage->path_new + storage->next_storage->len_path;
        storage->len_path_new_allow = PATH_MAX - storage->next_storage->len_path;
        if (storage->next_storage->half_duplex) {
            storage->io_mutex_need_lock_next = true;
            storage->io_mutex_need_lock = true;
            storage->next_io_mutex = &storage->next_storage->io_mutex;
        }
    }
    return 0;
}

int storages_init(struct storage *const storage_head) {
    for (struct storage *storage_current = storage_head; storage_current; storage_current = storage_current->next_storage) {
        if (storage_init(storage_current)) {
            pr_error("Failed to init storage '%s'\n", storage_current->path);
            return 1;
        }
    }
    return 0;
}

static int get_oldest(DIR *const dir, char *subpath_oldest, time_t *mtime_oldest, unsigned long *entries_count) {
    int const dir_fd = dirfd(dir);
    if (dir_fd < 0) {
        pr_error_with_errno("Failed to get fd of dir");
        return 1;
    }
    struct dirent *entry;
    *entries_count = 0;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '\0') {
            continue;
        }
        if (entry->d_name[0] == '.') {
            if (entry->d_name[1] == '\0') {
                continue;
            }
            if (entry->d_name[1] == '.' && entry->d_name[2] == '\0') {
                continue;
            }
        }
        if (!strcmp(entry->d_name, "lost+found")) {
            continue;
        }
        ++*entries_count;
        switch (entry->d_type) {
        case DT_REG: {
            struct stat st;
            if (fstatat(dir_fd, entry->d_name, &st, 0) < 0) {
                pr_error_with_errno("Failed to get stat of '%s'", entry->d_name);
                return 2;
            }
            if (st.st_mtim.tv_sec < *mtime_oldest) {
                *mtime_oldest = st.st_mtim.tv_sec;
                subpath_oldest[0] = '/';
                size_t const len_name = strlen(entry->d_name);
                strncpy(subpath_oldest + 1, entry->d_name, len_name);
                *(subpath_oldest + len_name + 1) = '\0';
            }
            break;
        }
        case DT_DIR: {
            int dir_sub_fd = openat(dir_fd, entry->d_name, O_RDONLY | O_DIRECTORY);
            if (dir_sub_fd < 0) {
                pr_error_with_errno("Failed to open sub folder '%s'", entry->d_name);
                return 3;
            }
            DIR *const dir_sub = fdopendir(dir_sub_fd);
            if (!dir_sub) {
                pr_error_with_errno("Failed to open subdir '%s' from fd", entry->d_name);
                close(dir_sub_fd);
                return 4;
            }
            size_t const len_name = strlen(entry->d_name);
            char *const subpath_oldest_recursive = subpath_oldest + len_name + 1;
            time_t const mtime_oldest_before = *mtime_oldest;
            unsigned long entries_count_recursive;
            if (get_oldest(dir_sub, subpath_oldest_recursive, mtime_oldest, &entries_count_recursive)) {
                pr_error("Failed to get oldest from subfolder '%s'\n", entry->d_name);
                closedir(dir_sub);
                return 5;
            }
            if (*mtime_oldest < mtime_oldest_before) {
                subpath_oldest[0] = '/';
                strncpy(subpath_oldest + 1, entry->d_name, len_name);
            }
            closedir(dir_sub);
            if (entries_count_recursive) {
                *entries_count += entries_count_recursive;
            } else {
                if (unlinkat(dir_fd, entry->d_name, AT_REMOVEDIR) < 0) {
                    pr_error_with_errno("Failed to remove empty subfolder '%s'", entry->d_name);
                }
                --*entries_count;
            }
            break;
        }
        }
    }
    return 0;
}

static int move_between_fs(char const *const path_old, char const *const path_new, struct storage *const storage) {
    struct stat st;
    if (stat(path_old, &st)) {
        pr_error_with_errno("Failed to get stat of old file '%s'", path_old);
        return 1;
    }
    int fin = open(path_old, O_RDONLY);
    if (fin < 0) {
        pr_error_with_errno("Failed to open old file '%s'", path_old);
        return 2;
    }
    int fout = open(path_new, O_WRONLY | O_CREAT, 0644);
    if (fout < 0) {
        pr_error_with_errno("Failed to open new file '%s'", path_new);
        close(fin);
        return 3;
    }
    size_t remain = st.st_size;
    ssize_t r;
    if (storage->io_mutex_need_lock) { /* Use two different branches to save time wasted on condition */
        while (remain) {
            if (storage->io_mutex_need_lock_this) {
                pthread_mutex_lock(&storage->io_mutex);
            }
            if (storage->io_mutex_need_lock_next) {
                pthread_mutex_lock(storage->next_io_mutex);
            }
            r = sendfile(fout, fin, NULL, remain);
            if (storage->io_mutex_need_lock_this) {
                pthread_mutex_unlock(&storage->io_mutex);
            }
            if (storage->io_mutex_need_lock_next) {
                pthread_mutex_unlock(storage->next_io_mutex);
            }
            if (r < 0) {
                close(fin);
                close(fout);
                pr_error_with_errno("Failed to send file '%s' -> '%s'", path_old, path_new);
                return 4;
            }
            remain -= r;
        }
    } else {
        while (remain) {
            r = sendfile(fout, fin, NULL, remain);
            if (r < 0) {
                close(fin);
                close(fout);
                pr_error_with_errno("Failed to send file '%s' -> '%s'", path_old, path_new);
                return 4;
            }
            remain -= r;
        }
    }
    close(fin);
    close(fout);
    if (unlink(path_old) < 0) {
        pr_error_with_errno("Failed to unlink old file '%s'", path_old);
    }
    return 0;
}

static int move_file(char const *const path_old, char const *const path_new, struct storage *const storage) {
    if (mkdir_recursive_only_parent(path_new, 0755)) {
        pr_error("Failed to create parent folders for '%s'", path_new);
        return 1;
    }
    if (rename(path_old, path_new) < 0) {
        switch (errno) {
        case ENOENT:
            pr_error("Old file '%s' does not exist now, did you remove it by yourself? Or is the disk broken? Ignore that for now", path_old);
            return 0;
        case EXDEV:
            if (move_between_fs(path_old, path_new, storage)) {
                pr_error("Failed to move '%s' to '%s' across fs\n", path_old, path_new);
                return 2;
            }
            break;
        default:
            pr_error_with_errno("Failed to rename '%s' to '%s'", path_old, path_new);
            return 3;
        }
    }
    return 0;
}


static int storage_clean(struct storage *const storage) {
    bool oneshot_clean = storage_oneshot_cleaner && storage->move_to_next;
    for (unsigned short i = 0; i < 0xffff; ++i) {
        while (storage->next_storage->cleaning) {
            pr_debug("Cleaner for '%s' waiting for cleaner for next storage '%s' to complete\n", storage->path, storage->next_storage->path);
            sleep(1);
        }
        *storage->subpath_oldest = '\0';
        rewinddir(storage->dir);
        time_t mtime_oldest = LONG_MAX;
        unsigned long entries_count;
        if (get_oldest(storage->dir, storage->subpath_oldest, &mtime_oldest, &entries_count)) {
            pr_error("Failed to get oldest in '%s'", storage->path);
            return 2;
        }
        if (*storage->subpath_oldest == '/') {
            pr_warn("Cleaning oldest file '%s' from storage '%s' (currently %lu entries)\n", storage->path_oldest, storage->path, entries_count);
            if (storage->move_to_next) {
                strncpy(storage->subpath_new, storage->subpath_oldest, storage->len_path_new_allow);
                if (move_file(storage->path_oldest, storage->path_new, storage)) {
                    pr_error("Failed to move file '%s' to '%s'\n", storage->path_oldest, storage->path_new);
                    return 3;
                }
                pr_warn("Moved file '%s' to '%s'\n", storage->path_oldest, storage->path_new);
            } else {
                if (unlink(storage->path_oldest) < 0) {
                    pr_error_with_errno("Failed to unlink file '%s'\n", storage->path_oldest);
                    return 3;
                }
                pr_warn("Removed file '%s'\n", storage->path_oldest);
            }
        }
        if (oneshot_clean) {
            return 0;
        }
        struct statvfs st;
        if (statvfs(storage->path, &st) < 0) {
            pr_error_with_errno("Failed to get vfs stat for '%s'", storage->path);
            return 1;
        }
        if (st.f_bfree >= storage->thresholds.to.free_blocks) {
            pr_warn("Cleaned %hu record files in storage '%s'\n", i + 1, storage->path);
            return 0;
        }
    }
    return 0;
}

static void *storage_clean_thread(void *arg) {
    long r = storage_clean((struct storage *)arg);
    return (void *)r;
}

int storages_clean(struct storage *const storage_head) {
    for (struct storage *storage = storage_head; storage; storage = storage->next_storage) {
        if (storage->cleaning) {
            long ret;
            int r = pthread_tryjoin_np(storage->cleaner_thread, (void **)&ret);
            switch (r) {
            case EBUSY:
                break;
            case 0:
                if (ret) {
                    pr_error("Cleaner for storage '%s' breaks with return value '%ld'\n", storage->path, ret);
                    return 1;
                }
                storage->cleaning = false;
                --running_cleaners;
                break;
            default:
                pr_error("Unexpected return from pthread_tryjoin_np: %d\n", r);
                return 2;
            }
        } else if (!storage_oneshot_cleaner || running_cleaners < max_cleaners) {
            struct statvfs st;
            if (statvfs(storage->path, &st) < 0) {
                pr_error_with_errno("Failed to get vfs stat for '%s'", storage->path);
                return 3;
            }
            if (st.f_bfree <= storage->thresholds.from.free_blocks) {
                storage->cleaning = true;
                ++running_cleaners;
                if (pthread_create(&storage->cleaner_thread, NULL, storage_clean_thread, (void *)storage)) {
                    pr_error("Failed to create pthread for storage cleaner for storage '%s'\n", storage->path);
                    return 4;
                }
                pr_warn("Started to clean storage '%s'\n", storage->path);
            }
        }
    }
    return 0;
}