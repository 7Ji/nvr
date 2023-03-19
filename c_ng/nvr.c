#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>

#define pr_error(format, arg...) \
    fprintf(stderr, "%s:%d:error: "format, __FUNCTION__, __LINE__, ##arg)

#define pr_error_with_errno(format, arg...) \
    pr_error(format", errno: %d, error: %s\n", ##arg, errno, strerror(errno))

#define pr_warn(format, arg...) \
    printf("%s:%d:warn: "format, __FUNCTION__, __LINE__, ##arg)

#ifdef DEBUGGING
#define pr_debug(format, arg...) \
    printf("%s:%d:debug: "format, __FUNCTION__, __LINE__, ##arg)
#else
#define pr_debug(format, arg...)
#endif

struct threshold {
    unsigned short from_free_percent;
    unsigned short to_free_percent;
};

struct space {
    // fsblkcnt_t free_blocks;
    // fsblkcnt_t total_blocks;
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

struct camera {
    struct camera *next_camera;
    char name[NAME_MAX];
    unsigned short len_name;
    char strftime[NAME_MAX];
    char url[PATH_MAX];
    struct storage const *storage;
    // char path_parent[PATH_MAX];
    // unsigned short len_path_parent;
    pthread_t recorder_pthread;
};

struct fds {
    int null;
    int stdout_dup;
    int stderr_dup;
};

static struct fds fds = { -1, -1, -1 };

static inline void help() {
    puts(
        "./nvr --storage [storage definition] (--storage [storage definition] (--storage [storage definition] (...)))\n"
        "      --camera [camera definition] (--camera [camera definition] (--camera [camera definition] (...)))\n"
        "      --help\n"
        "      --version\n\n"
        "  - [storage deinition]: [path]:[thresholds]\n"
        "    - [path]: folder name or path, relative or absolute both fine\n"
        "    - [thresholds]: [from]:[to]\n"
        "      - [from]: when free space <= this percent, triggers cleaning\n"
        "      - [to]: when free space >= this percent, stops cleaning\n"
        "  - [camera definition]: [name]:[strftime]:[url]\n"
        "    - [name]: used to generate output name if strftime not set, or only for reminder if strftime set\n"
        "    - [strftime]: will be used to construct the output name, without suffix, appended after storage\n"
        "    - [url]: a valid input url for ffmpeg\n"
    );
}

static inline void version() {
    puts("No version message available yet");
}

unsigned short parse_argument_seps(char const *const arg, char const *seps[], unsigned short sep_max, char const **const end) {
    unsigned short sep_id = 0;
    for (char const *c = arg;; ++c) {
        switch (*c) {
        case ':':
            if (sep_id != sep_max) {
                seps[sep_id++] = c;
            }
            break;
        case '\0':
            *end = c;
            return sep_id;
        default:
            break;
        }
    }
}

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

struct storage *parse_argument_storage(char const *const arg) {
    pr_debug("Parsing storage definition: '%s'\n", arg);
    char const *seps[2];
    char const *end = NULL;
    unsigned short sep_id = parse_argument_seps(arg, seps, 2, &end);
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
    unsigned from_free_percent = strtoul(seps[0] + 1, NULL, 10);
    unsigned to_free_percent = strtoul(seps[1] + 1, NULL, 10);
    if (from_free_percent >= to_free_percent) {
        pr_error("From free percent (%hu%%) can not be equal to or larger than to free percent (%hu%%): '%s'\n", from_free_percent, to_free_percent, arg);
        return NULL;
    }
    if (to_free_percent > 100) {
        pr_error("To free percent (%hu%%) can not be larger than 100%%: '%s'\n", to_free_percent, arg);
        return NULL;
    }
    struct storage *storage = malloc(sizeof *storage);
    if (!storage) {
        pr_error_with_errno("Failed to allocate memory for storage");
        return NULL;
    }
    strncpy(storage->path, arg, len_path);
    storage->path[len_path] = '\0';
    storage->len_path = len_path;
    storage->threshold.from_free_percent = from_free_percent;
    storage->threshold.to_free_percent = to_free_percent;
    storage->next_storage = NULL;
    pr_debug("Storage defitnition: path: '%s' (length %hu), clean from free percent: %hu%%, clean to free percent: %hu%%\n", storage->path, storage->len_path, storage->threshold.from_free_percent, storage->threshold.to_free_percent);
    return storage;
}

static inline int mkdir_allow_exist(char const *const path, mode_t mode) {
    if (mkdir(path, mode) < 0) {
        if (errno != EEXIST) {
            pr_error_with_errno("Failed to create directory '%s'", path);
            return 1;
        }
    }
    return 0;
}

int mkdir_recursive(char const *const path, mode_t mode) {
    char _path[PATH_MAX];
    strncpy(_path, path, PATH_MAX);
    for (char *p = _path + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir_allow_exist(_path, mode)) {
                pr_error("Failed to mkdir '%s' recursively for parent '%s'", path, _path);
                return 1;
            }
            *p = '/';
        }
    }
    if (mkdir_allow_exist(_path, mode)) {
        pr_error("Failed to mkdir '%s' recursively for itself", path);
        return 2;
    }
    return 0;
}

int mkdir_recursive_only_parent(char const *const path, mode_t mode) {
    char _path[PATH_MAX];
    strncpy(_path, path, PATH_MAX);
    for (char *p = _path + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir_allow_exist(_path, mode)) {
                pr_error("Failed to mkdir (parent-only) '%s' recursively for parent '%s'", path, _path);
                return 1;
            }
            *p = '/';
        }
    }
    return 0;
}

int storage_init(struct storage *const storage) {
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
    storage->space.from_free_blocks = st.f_blocks * storage->threshold.from_free_percent / 100;
    storage->space.to_free_blocks = st.f_blocks * storage->threshold.to_free_percent / 100;
    if (storage->space.from_free_blocks > st.f_blocks) {
        storage->space.from_free_blocks = st.f_blocks;
    }
    if (storage->space.to_free_blocks > st.f_blocks) {
        storage->space.to_free_blocks = st.f_blocks;
    }
    if (!storage->space.to_free_blocks) {
        pr_error("Storage '%s' trigger: clean space to free blocks is 0, which will never work\n", storage->path);
        return 3;
    }
    if (storage->space.from_free_blocks >= storage->space.to_free_blocks) {
        storage->space.from_free_blocks = storage->space.to_free_blocks - 1;
    }
    if (!(storage->dir = opendir(storage->path))) {
        pr_error_with_errno("Failed to open storage '%s'", storage->path);
        return 4;
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

int camera_init(struct camera const *const camera) {
    pr_warn("Checking if url '%s' works\n", camera->url);
    pid_t child = fork();
    switch (child) {
    case -1:
        pr_error_with_errno("Failed to fork to test if url '%s' works", camera->url);
        return 1;
    case 0: {
        if (dup2(fds.null, STDOUT_FILENO) < 0 || dup2(fds.null, STDERR_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        char ffprobe[] = "/usr/bin/ffprobe";
        char url[PATH_MAX];
        strncpy(url, camera->url, PATH_MAX);
        char *const argv[] = {
            ffprobe,
            url,
            NULL
        };
        execv(ffprobe, argv);
        if (dup2(fds.stdout_dup, STDOUT_FILENO) >= 0 && dup2(fds.stderr_dup, STDERR_FILENO) >=0) {
            pr_error_with_errno("Failed to exec ffprobe to test if url '%s' works", camera->url);
        }
        exit(EXIT_FAILURE);
    }
    default:
        break;
    }
    int status;
    pid_t waited = waitpid(child, &status, 0);
    switch (waited) {
    case -1:
        pr_error_with_errno("Failed to wait for child");
        return 2;
    case 0:
        pr_error("Unexpeted waited pid 0\n");
        return 3;
    default:
        break;
    }
    if (waited != child) {
        pr_error("Unexpecetd waited pid: want %d but got %d\n", child, waited);
        return 4;
    }
    if (status) {
        pr_error("Camera url '%s' does not work, ffprobe returned %d\n", camera->url, status);
        return 5;
    }
    pr_warn("Camera url '%s' works\n", camera->url);
    return 0;
}

void *camera_init_thread(void *arg) {
    long r = camera_init((struct camera *)(arg));
    return (void *)r;
}

int cameras_init(struct camera *const camera_head, struct storage const *const storage_head) {
    if ((fds.stdout_dup = dup(STDOUT_FILENO)) < 0) {
        pr_error_with_errno("Failed to duplicate stdout fd");
        return 1;
    }
    if ((fds.stderr_dup = dup(STDERR_FILENO)) < 0) {
        pr_error_with_errno("Failed to duplicate stderr fd");
        return 2;
    }
    if ((fds.null = open("/dev/null", O_WRONLY | O_CREAT, 0666)) < 0) {
        pr_error_with_errno("Failed to open /dev/null");
        return 3;
    }
    unsigned short camera_count = 0;
    for (struct camera *camera_current = camera_head; camera_current; camera_current = camera_current->next_camera) {
        camera_current->storage = storage_head;
        // strncpy(camera_current->)
        // if (snprintf(camera_current->path_strftime, PATH_MAX, "%s/%s", storage_head->path, camera_current->strftime) < 0) {
        //     pr_error_with_errno("Failed to generate path strftime for camera with strftime '%s' and storage '%s'", camera_current->strftime, storage_head->path);
        //     return 4;
        // }
        // snprintf(camera_current->)
        ++camera_count;
    }
    if (camera_count > 1) { /* Only use threaded initialization when there are multiple cameras */
        pthread_t *pthreads = malloc(sizeof *pthreads * camera_count);
        if (!pthreads) {
            pr_error_with_errno("Failed to allocate memory for pthreads to init cameras");
            return 5;
        }
        pthread_t *pthread_current = pthreads;
        for (struct camera *camera_current = camera_head; camera_current; camera_current = camera_current->next_camera) {
            if (pthread_create(pthread_current++, NULL, camera_init_thread, (void *)(camera_current))) {
                pr_error("Failed to create pthread to init camera for '%s'\n", camera_current->url);
                free(pthreads);
                return 6;
            }
        }
        for (unsigned short pthread_id = 0; pthread_id < camera_count; ++pthread_id) {
            long ret;
            if (pthread_join(pthreads[pthread_id], (void **)&ret)) {
                pr_error("Failed to join pthread\n");
                free(pthreads);
                return 7;
            }
            if (ret) {
                pr_error("Failed to init camera\n");
                free(pthreads);
                return 8;
            }
        }
        free(pthreads);
    } else {
        if (camera_init(camera_head)) {
            pr_error("Failed to init camera for '%s'\n", camera_head->url);
            return 9;
        }
    }
    return 0;
}

int get_oldest(DIR *const dir, char *subpath_oldest, time_t *mtime_oldest, unsigned long *entries_count) {
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

int move_between_fs(char const *const path_old, char const *const path_new) {
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
    close(fin);
    close(fout);
    if (unlink(path_old) < 0) {
        pr_error_with_errno("Failed to unlink old file '%s'", path_old);
    }
    return 0;
}

int move_file(char const *const path_old, char const *const path_new) {
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
            if (move_between_fs(path_old, path_new)) {
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


int storage_watcher(struct storage const *const storage) {
    char path_oldest[PATH_MAX];
    strncpy(path_oldest, storage->path, storage->len_path + 1);
    char *subpath_oldest = path_oldest + storage->len_path;
    if (*subpath_oldest) {
        pr_error("Path of storage '%s' not properly ended or length %hu is not right\n", storage->path, storage->len_path);
        return 1;
    }
    char path_new[PATH_MAX];
    char *subpath_new;
    size_t len_path_new_allow;
    bool const move_to_next = storage->next_storage;
    if (move_to_next) {
        strncpy(path_new, storage->next_storage->path, storage->next_storage->len_path);
        subpath_new = path_new + storage->next_storage->len_path;
        len_path_new_allow = PATH_MAX - storage->next_storage->len_path;
    }
    while (true) {
        struct statvfs st;
        if (statvfs(storage->path, &st) < 0) {
            pr_error_with_errno("Failed to get vfs stat for '%s'", storage->path);
            return 1;
        }
        if (st.f_bfree <= storage->space.from_free_blocks) {
            for (unsigned short i = 0; i < 0xffff; ++i) {
                *subpath_oldest = '\0';
                rewinddir(storage->dir);
                time_t mtime_oldest = LONG_MAX;
                unsigned long entries_count;
                if (get_oldest(storage->dir, subpath_oldest, &mtime_oldest, &entries_count)) {
                    pr_error("Failed to get oldest in '%s'", storage->path);
                    return 2;
                }
                if (*subpath_oldest == '/') {
                    pr_warn("Cleaning oldest file '%s' from storage '%s' (currently %lu entries)\n", path_oldest, storage->path, entries_count);
                    if (move_to_next) {
                        strncpy(subpath_new, subpath_oldest, len_path_new_allow);
                        if (move_file(path_oldest, path_new)) {
                            pr_error("Failed to move file '%s' to '%s'\n", path_oldest, path_new);
                            return 3;
                        }
                        pr_warn("Moved file '%s' to '%s'\n", path_oldest, path_new);
                    } else {
                        if (unlink(path_oldest) < 0) {
                            pr_error_with_errno("Failed to unlink file '%s'\n", path_oldest);
                            return 3;
                        }
                        pr_warn("Removed file '%s'\n", path_oldest);
                    }
                }
                if (statvfs(storage->path, &st) < 0) {
                    pr_error_with_errno("Failed to get vfs stat for '%s'", storage->path);
                    return 1;
                }
                if (st.f_bfree >= storage->space.to_free_blocks) {
                    pr_warn("Cleaned %hu record files in storage '%s'\n", i, storage->path);
                    break;
                }
            }
        }
        sleep(1);
    }
    return 0;
}

void *storage_watch_thread(void *arg) {
    long r = storage_watcher((struct storage *)arg);
    return (void *)r;
}

int storages_start(struct storage *const storage_head) {
    for (struct storage *storage_current = storage_head; storage_current; storage_current = storage_current->next_storage) {
        if (pthread_create(&storage_current->watcher_pthread, NULL, storage_watch_thread, (void *)storage_current)) {
            pr_error("Failed to create pthread for storage watcher for '%s'\n", storage_current->path);
            return 1;
        }
    }
    return 0;
}

int camera_recorder(struct camera const *const camera) {
    char path[PATH_MAX];
    strncpy(path, camera->storage->path, camera->storage->len_path);
    path[camera->storage->len_path] = '/';
    char *const subpath = path + camera->storage->len_path + 1;
    size_t const len_subpath_max = PATH_MAX - camera->storage->len_path - 1;
    pid_t last_child = 0;
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
        char duration[16];
        if (snprintf(duration, 16, "%ld", time_diff + 10) < 0) {
            pr_error_with_errno("Failed to generate duration string for camera of url '%s'", camera->url);
            return 2;
        }
        pr_warn("Recording from '%s' to '%s', duration %ss\n", camera->url, path, duration);
        pid_t child = fork();
        switch (child) {
        case -1:
            pr_error_with_errno("Failed to fork for camera recorder for camera of url '%s'", camera->url);
            return 3;
        case 0: {
            if (dup2(fds.null, STDOUT_FILENO) < 0 || dup2(fds.null, STDERR_FILENO) < 0) {
                exit(EXIT_FAILURE);
            }
            char ffmpeg[] = "/usr/bin/ffmpeg";
            char url[PATH_MAX];
            strncpy(url, camera->url, PATH_MAX);
            char *argv[] = {
                ffmpeg,
                "-use_wallclock_as_timestamps", "1",
                /* Input */
                "-i", url,
                /* Codec */
                "-c", "copy",
                /* Duration */
                "-t", duration,
                /* Output */
                "-y", path,
                /* Sentry */
                NULL
            };
            execv(ffmpeg, argv);
            if (dup2(fds.stdout_dup, STDOUT_FILENO) >= 0 && dup2(fds.stderr_dup, STDERR_FILENO) >=0) {
                pr_error_with_errno("Failed to exec ffmpeg to record from url '%s'", camera->url);
            }
            exit(EXIT_FAILURE);
        }
        default:
            break;
        }
        while (time_now < time_future) {
            int status;
            pid_t waited = waitpid(child, &status, WNOHANG);
            switch (waited) {
            case -1:
                pr_error_with_errno("Failed to wait for child ffmpeg %d", child);
                return 2;
            case 0:
                break;
            default:
                if (waited == child) {
                    child = 0;
                    break;
                } else {
                    pr_error("Unexpected waited child: want %d but get %d\n", child, waited);
                    return 3;
                }
            }
            if (last_child) {
                switch (waited = waitpid(last_child, &status, WNOHANG)) {
                case -1:
                    pr_error_with_errno("Failed to wait for last child ffmpeg %d", last_child);
                    return 4;
                case 0:
                    break;
                default:
                    if (waited == last_child) {
                        last_child = 0;
                        break;
                    } else {
                        pr_error("Unexpected waited last child: want %d but get %d\n", last_child, waited);
                        return 5;
                    }
                }
            }
            if (!child) {
                break;
            }
            time_diff = time_future - (time_now = time(NULL));
            sleep(time_diff > 10 ? 10 : time_diff);
        }
        if (child) {
            if (last_child) {
                int status;
                pid_t waited = waitpid(last_child, &status, WNOHANG);
                switch (waited) {
                case -1:
                    pr_error_with_errno("Failed to wait for last child ffmpeg %d", last_child);
                    return 6;
                case 0:
                    if (kill(last_child, SIGINT)) {
                        pr_error_with_errno("Failed to sent SIGINT to last child ffmpeg %d", last_child);
                        return 7;
                    }
                    if ((waited = waitpid(last_child, &status, 0)) <= 0) {
                        pr_error_with_errno("Failed to wait for killed last child ffmpeg %d", last_child);
                        return 8;
                    }
                    if (waited != last_child) {
                        pr_error("Unexpected waited killed last child: want %d but get %d\n", last_child, waited);
                        return 9;
                    }
                    break;
                default:
                    if (waited != last_child) {
                        pr_error("Unexpected waited last child: want %d but get %d\n", last_child, waited);
                        return 10;
                    }
                    break;
                }
            }
            last_child = child;
        }
    }
    return 0;
}

void *camera_record_thread(void *arg) {
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

int main(int const argc, char const *const argv[]) {
    if (argc < 2) {
        pr_error("Arguments too few\n");
        help();
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
                help();
                return 0;
            }
            if (!strncmp(arg, "version", 8)) {
                version();
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
            help();
            return 6;
        }
    }
    if (!camera_head || !camera_last) {
        pr_error("No camera defined\n");
        help();
        return 7;
    }
    if (!storage_head || !storage_last) {
        pr_error("No storage defined\n");
        help();
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