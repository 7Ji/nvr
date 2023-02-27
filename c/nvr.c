#define _GNU_SOURCE

/* Headers */

/* ISO C Standard */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>     
#include <stdbool.h>
#include <string.h>
#include <time.h>
/* POSIX standard */
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
/* Linux */
#include <linux/limits.h>
#include <sys/sendfile.h>

/* Definitions */

#define STORAGE_NAME_MAXLEN     NAME_MAX+1
#define CAMERA_NAME_MAXLEN      NAME_MAX / 2
#define CAMERA_URL_MAXLEN       PATH_MAX
#define CAMERA_STRFTIME_MAXLEN  PATH_MAX

/* Structs */

/**
 * @brief a struct to represent the storage clean threshold
 */
struct Threshold {
    /**
     * @brief the percent of free space that would trigger cleaning
    */
    unsigned short from;
    /**
     * @brief the percent of free space that would stop a triggered cleaning
    */
    unsigned short to;
};

/**
 * @brief a struct to represent the storage
*/
struct Storage {
    /**
     * @brief the name of storage, potentially a path to the folder
    */
    char name[STORAGE_NAME_MAXLEN];
    /**
     * @brief thresholds to start and end cleaning
    */
    struct Threshold threshold;
    /**
     * @brief pid of the cleaning watcher thread for this thread
    */
    pid_t watcher;
    /**
     * @brief next storage
    */
    struct Storage *next;
};

/**
 * @brief a struct to represent a camera
*/
struct Camera {
    /**
     * @brief the name of the camera, would be used as part of the filename
    */
    char name[CAMERA_NAME_MAXLEN];
    /**
     * @brief the url of the camera, woule be used as input of ffmpeg
    */
    char url[CAMERA_URL_MAXLEN];
    /**
     * @brief the strftime formatter, would be used to generate the output path of ffmpeg
    */
    char strftime[CAMERA_STRFTIME_MAXLEN];
    /**
     * @brief pid of the record worker
    */
    pid_t recorder;
    /**
     * @brief next camera
    */
    struct Camera *next;
};

/**
 * @brief a struct to hold disk space
*/
struct Space {
    /**
     * @brief the current free blocks
    */
    fsblkcnt_t free;
    /**
     * @brief the total blocks
    */
    fsblkcnt_t total;
    /**
     * @brief the threshold of free when cleaning would be triggered
    */
    fsblkcnt_t from;
    /**
     * @brief the threshold of free when cleaning would end
    */
    fsblkcnt_t to;
};

/* Functions */

/**
 * @brief function to output help message
*/
void help() {
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
        "  - [camera definition]: [name]:[url]\n"
        "    - [name]: \n"
        "    - [url]: a valid input url for ffmpeg\n"
    );
}

/**
 * @brief function to output version
*/
void version() {
    puts("No version message available yet");
}

/**
 * @brief wrapper around strncpy()
 * 
 * @param dst pointer to the destination area the string should be copied to
 * @param src pointer to the source area the string should be copied from
 * @param len the maximum length of string that should be copied, excluding NULL
 * 
 * @returns 0 for success, non-0 for failure
*/
int safe_strncpy(char *const dst, char const *const src, size_t len) {
    if (dst == NULL) {
        fputs("Give up to copy string, destination is NULL\n", stderr);
        return 1;
    }
    if (src == NULL) {
        fputs("Give up to copy string, source is NULL\n", stderr);
        return 2;
    }
    if (src[0] == '\0') {
        fputs("Give up to copy string, source is empty\n", stderr);
        return 3;
    }
    size_t len_actual = strlen(src);
    if (len_actual >= len) {
        fputs("Give up to copy string, source longer than requested length\n", stderr);
        return 4;
    }
    strncpy(dst, src, len);
    return 0;
}

/**
 * @brief parse the input string to a struct Threshold
 * 
 * @param threshold pointer to the struct Threshold that should be filled
 * @param string the source string that should be parsed
 * 
 * @returns 0 for success, non-0 for failure
*/
int parse_threshold(struct Threshold *const threshold, char const *const string) {
    if (sscanf(string, "%hu:%hu", &threshold->from, &threshold->to) != 2) {
        fprintf(stderr, "Failed to parse '%s' as threshold\n", string);
        return 1;
    }
    if (threshold->from >= 100) {
        fprintf(stderr, "Threshold from too large: %hu\n", threshold->from);
        return 2;
    }
    if (threshold->to > 100) {
        fprintf(stderr, "Threshold to too large: %hu\n", threshold->to);
        return 3;
    }
    if (threshold->to <= threshold->from) {
        fprintf(stderr, "Threshold to can not be smaller or equal to from: %hu <= %hu\n", threshold->to, threshold->from);
        return 4;
    }
    return 0;
}


struct Storage *parse_storage(char const *const string) {
    struct Storage *storage = malloc(sizeof *storage);
    if (!storage) {
        fprintf(stderr, "Failed to allocate memory for storage\n");
        return NULL;
    }
    memset(storage, 0, sizeof *storage);
    char const *seps[2] = {NULL, NULL};
    unsigned short sep_id = 0;
    for (char const *c = string; *c && sep_id < 2; ++c) {
        if (*c == ':' && *(c + 1) != '\0') {
            seps[sep_id++] = c;
        }
    }
    if (sep_id < 2) {
        fprintf(stderr, "Storage definition incomplete or empty (should be [path]:[from]:[to]): %s\n", string);
        free(storage);
        return NULL;
    }
    if (seps[0] == string) {
        fprintf(stderr, "Storage path is empty: %s\n", string);
        free(storage);
        return NULL;
    }
    if (parse_threshold(&storage->threshold, seps[0] + 1)) {
        fprintf(stderr, "Storage threshold invalid: %s\n", string);
        free(storage);
        return NULL;
    }
    strncpy(storage->name, string, seps[0] - string);
    return storage;
}

/**
 * @brief parse the input string to a struct Camera
 * 
 * @param string the source string that should be parsed
 * 
 * @returns 0 for success, non-0 for failure
*/
struct Camera *parse_camera(char const *const string) {
    struct Camera *camera = malloc(sizeof *camera);
    if (!camera) {
        fprintf(stderr, "Failed to allocate memory for camera\n");
        return NULL;
    }
    memset(camera, 0, sizeof *camera);
    bool parsed = false;
    for (char const *c = string; *c; ++c) {
        if (*c == ':' && *(c + 1) != '\0') {
            strncpy(camera->name, string, c - string);
            strncpy(camera->url, c + 1, CAMERA_URL_MAXLEN);
            parsed = true;
            break;
        }
    }
    if (!parsed) {
        fprintf(stderr, "Camera definition incomplete or empty (should be [name]:[url]): %s\n", string);
        free(camera);
        return NULL;
    }
    printf("Camera added with name '%s', url '%s'\n", camera->name, camera->url);
    return camera;
}

int complete_camera(struct Camera *const camera, struct Storage const *const hot) {
    snprintf(camera->strftime, CAMERA_STRFTIME_MAXLEN, "%s/%s_%%Y%%m%%d_%%H%%M%%S.mkv", hot->name, camera->name);
    return 0;
}

static inline int update_space_common(char const *const path, struct statvfs *const st) {
    int r = statvfs(path, st);
    switch (r) {
        case 0:
            return 0;
        case -1:
            fprintf(stderr, "Failed to get disk space of %s, errno: %d, error: %s\n", path, errno, strerror(errno));
            return 1;
        default:
            fprintf(stderr, "Impossible return value from statvfs: %d\n", r);
            return 2;
    }
}

static inline int update_space_init(struct Space *const space, char const *const path) {
    struct statvfs st;
    if (update_space_common(path, &st)) {
        return 1;
    }
    space->free = st.f_bfree;
    space->total = st.f_blocks;
    return 0;
}

static inline int update_space_further(struct Space *const space, char const *const path) {
    struct statvfs st;
    if (update_space_common(path, &st)) {
        return 1;
    }
    space->free = st.f_bfree;
    return 0;
}

static inline int watcher_init(struct Space *const space, DIR **const dir, struct Storage const *const storage) {
    if (mkdir(storage->name, 0700) == -1) {
        switch (errno) {
            case EEXIST:
                break;
            default:
                fprintf(stderr, "Failed to create directory %s, errno: %d, error: %s\n", storage->name, errno, strerror(errno));
                return 1;
        }
    }
    if (update_space_init(space, storage->name)) {
        return 2;
    }
    space->from = space->total / 100 * storage->threshold.from;
    space->to = space->total / 100 * storage->threshold.to;
    if ((*dir = opendir(storage->name)) == NULL) {
        fprintf(stderr, "Failed to open folder '%s', errno: %d, error: %s\n", storage->name, errno, strerror(errno));
        return 3;
    }
    return 0;
}

int get_oldest(char *const path_old, char *const name_old, DIR *dir) {
    struct dirent *entry;
    struct stat current;
    time_t oldest_mtime = 0;
    char path_current[PATH_MAX];
    name_old[0] = '\0';
    char *name_current = stpncpy(path_current, path_old, name_old - path_old);
    while ((entry = readdir(dir))) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        switch (entry->d_name[0]) {
            case '.':
                switch (entry->d_name[1]) {
                    case '\0': // . itself
                        continue;
                    case '.': 
                        if (entry->d_name[2] == '\0') continue; // .. parent
                    default:
                        break;
                }
                break;
            case '\0': // Empty name, WTF?
                continue;
            default:
                break;
        }
        strncpy(name_current, entry->d_name, NAME_MAX);
        // if (strncpy(name_current, entry->d_name, NAME_MAX) < 0) {
        //     fprintf(stderr, "Failed to print file '%s' name to path, errno: %d, error: %s\n", entry->d_name, errno, strerror(errno));
        //     continue;
        // }
        if (stat(path_current, &current)) {
            fprintf(stderr, "Failed to get stat of file '%s': errno: %d, error: %s\n", entry->d_name, errno, strerror(errno));
            continue;
        }
        if (current.st_mtim.tv_sec < oldest_mtime || oldest_mtime <= 0) {
            oldest_mtime = current.st_mtim.tv_sec;
            strncpy(name_old, entry->d_name, NAME_MAX);
        }
    }
    // *len = strnlen(oldest, NAME_MAX);
    // if (*len >= NAME_MAX && oldest[NAME_MAX]) {
    //     fprintf(stderr, "Name too long: %s\n", oldest);
    //     return 1;
    // }
    return 0;
}

int move_between_fs(char const *const path_old, char const *const path_new) {
    struct stat st;
    if (stat(path_old, &st)) {
        fprintf(stderr, "Failed to get stat of file '%s': errno: %d, error: %s\n", path_old, errno, strerror(errno));
        return 1;
    }
    int fin = open(path_old, O_RDONLY);
    if (fin < 0) {
        fprintf(stderr, "Failed to open old file '%s': errno: %d, error: %s\n", path_old, errno, strerror(errno));
        return 2;
    }
    int fout = open(path_new, O_WRONLY | O_CREAT, 0644);
    if (fout < 0) {
        fprintf(stderr, "Failed to open new file '%s': errno: %d, error: %s\n", path_new, errno, strerror(errno));
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
            fprintf(stderr, "Failed to send file '%s' -> '%s', errno: %d, error: %s\n", path_old, path_new, errno, strerror(errno));
            return 4;
        }
        remain -= r;
    }
    close(fin);
    close(fout);
    if (unlink(path_old) < 0) {
        fprintf(stderr, "Failed to unlink old file '%s', errno: %d, error: %s\n", path_old, errno, strerror(errno));
    }
    return 0;
}

int common_watcher(struct Storage const *const storage) {
    struct Space space = {0};
    DIR *dir;
    if (watcher_init(&space, &dir, storage)) {
        fprintf(stderr, "Failed to begin watching for folder '%s'\n", storage->name);
        return 1;
    }
    char path_old[PATH_MAX];
    char path_new[PATH_MAX];
    char *name_old = stpncpy(path_old, storage->name, NAME_MAX);
    // size_t len_dir_old = name_old - path_old;
    char *name_new = NULL;
    // size_t len_dir_new = 0;
    if (storage->next) {
        name_new = stpncpy(path_new, storage->next->name, NAME_MAX);
        // len_dir_new = name_new - path_new;
        *(name_new++) = '/';
    }
    *(name_old++) = '/';
    unsigned short cleaned;
    while (true) {
        if (update_space_further(&space, storage->name)) {
            fprintf(stderr, "Failed to update disk space for folder '%s'\n", storage->name);
            closedir(dir);
            return 2;
        }
        if (space.free <= space.from) {
            for (cleaned = 0; cleaned < 0xffff; ++cleaned) { // Limit 100, to avoid endless loop
                if (get_oldest(path_old, name_old, dir)) {
                    fprintf(stderr, "Failed to get oldest file in folder '%s'\n", storage->name);
                    closedir(dir);
                    return 3;
                }
                rewinddir(dir);
                if (!name_old[0]) {
                    /* No oldest file found */
                    break;
                }
                // snprintf(path_old, PATH_MAX, "%s/%s", storage->name, oldest);
                if (storage->next) {
                    strncpy(name_new, name_old, NAME_MAX);
                    // snprintf(path_new, PATH_MAX, "%s/%s", cold->name, oldest);
                    /* There is still colder layer, move to it */
                    printf("Cleaning '%s', moving to '%s'...\n", path_old, path_new);
                    if (rename(path_old, path_new) < 0) {
                        switch (errno) {
                            case ENOENT:
                                fprintf(stderr, "Warning: source file '%s' does not exist now, did you remove it by yourself? Or is the disk broken?\n", path_old);
                                break;
                            case EXDEV:
                                move_between_fs(path_old, path_new);
                                break;
                            default:
                                fprintf(stderr, "Warning: wierd things happened when trying to move '%s', errno: %d, error: %s\n", path_old, errno, strerror(errno));
                                break;
                        }
                    }
                } else {
                    /* There is no colder layer, just remove the file */
                    printf("Cleaning '%s', removing...\n", path_old);
                    unlink(path_old);
                    /* Don't care about result */
                    // if (unlink(oldest) < 0) {
                    //     fprintf(stderr, "Failed to remove file '%s', errno: %d, error: %s\n", oldest, errno, strerror(errno));
                    //     // return 4; Just continue
                    // }
                }
                if (update_space_further(&space, storage->name)) {
                    fprintf(stderr, "Failed to update disk space for folder '%s' when cleaning\n", storage->name);
                    closedir(dir);
                    return 4;
                }
                if (space.free >= space.to) {
                    break;
                }
            }
            printf("Watcher: cleaned %hu files\n", cleaned);
            sleep(600);
        } else {
            sleep(60);
        }
    }
    closedir(dir);
    return 0;
}

void camera_ffmpeg(char const *const url, time_t duration,  char const *const path) {
    if (duration < 60) {
        duration = 60;
    }
    duration += 10;
    char duration_str[64];
    snprintf(duration_str, 64, "%ld", duration);
    printf("Camera ffmpeg: recording '%s' to '%s', duration %s\n", url, path, duration_str);
    int fd_null = open("/dev/null", O_WRONLY | O_CREAT, 0666);
    dup2(fd_null, 1);
    dup2(fd_null, 2);
    if (execl(
        /* Executable */
        "/usr/bin/ffmpeg",
        /* Itself */
        "/usr/bin/ffmpeg",
        /* Input */
        "-use_wallclock_as_timestamps", "1",
        "-i", url,
        /* Codec */
        "-c", "copy",
        /* Duration */
        "-t", duration_str,
        /* Output */
        "-y", path,
        /* Sentry */
        NULL
    )) {
        fprintf(stderr, "Failed to call ffmpeg, errno: %d, error: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

int camera_recorder(struct Camera const *const camera) {
    pid_t child = 0;
    pid_t last_child = 0;
    pid_t waited;
    time_t time_now, time_future, time_diff;
    struct tm tms_now, tms_future;
    int minute;
    char path[PATH_MAX];
    int status;
    while (true) {
        time_now = time(NULL);
        localtime_r(&time_now, &tms_now);
        path[0] = '\0';
        strftime(path, PATH_MAX, camera->strftime, &tms_now);
        tms_future = tms_now;
        minute = (tms_now.tm_min + 11) / 10 * 10;
        if (minute >= 60) {
            tms_future.tm_min = minute - 60;
            ++tms_future.tm_hour;
        } else {
            tms_future.tm_min = minute;
        }
        tms_future.tm_sec = 0;
        time_future = mktime(&tms_future);
        time_diff = time_future - time_now;
        switch (child = fork()) {
            case -1:
                fprintf(stderr, "Camera recorder for %s: failed to fork, errno: %d, error: %s\n", camera->name, errno, strerror(errno));
                return 1;
            case 0:
                camera_ffmpeg(camera->url, time_diff, path);
                fputs("Child ffmpeg worker returned when it shouldn't, this is impossible!\n", stderr);
                return -1;
            default:
                break;
        }
        while (time_now < time_future) {
            switch (waited = waitpid(child, &status, WNOHANG)) {
                case -1:
                    fprintf(stderr, "Failed to wait for child ffmpeg %d, errno: %d, error: %s\n", child, errno, strerror(errno));
                    return 2;
                case 0:
                    break;
                default:
                    if (waited == child) {
                        child = 0;
                        break;
                    } else {
                        fprintf(stderr, "Unexpected waited child: want %d but get %d\n", child, waited);
                        return 3;
                    }
            }
            if (!child) {
                break;
            }
            if (last_child) {
                switch (waited = waitpid(last_child, &status, WNOHANG)) {
                    case -1:
                        fprintf(stderr, "Failed to wait for last child ffmpeg %d, errno: %d, error: %s\n", last_child, errno, strerror(errno));
                        return 4;
                    case 0:
                        break;
                    default:
                        if (waited == last_child) {
                            last_child = 0;
                            break;
                        } else {
                            fprintf(stderr, "Unexpected waited last child: want %d but get %d\n", last_child, waited);
                            return 5;
                        }
                }
            }
            time_diff = time_future - (time_now = time(NULL));
            sleep(time_diff > 10 ? 10 : time_diff);
        }
        if (child) {
            if (last_child) {
                switch (waited = waitpid(last_child, &status, WNOHANG)) {
                    case -1:
                        fprintf(stderr, "Failed to wait for last child ffmpeg %d, errno: %d, error: %s\n", last_child, errno, strerror(errno));
                        return 6;
                    case 0:
                        if (kill(last_child, SIGINT)) {
                            fprintf(stderr, "Failed to sent SIGINT to last child ffmpeg %d, errno: %d, error: %s\n", last_child, errno, strerror(errno));
                            return 7;
                        }
                        if ((waited = waitpid(last_child, &status, 0)) <= 0) {
                            fprintf(stderr, "Failed to wait for killed last child ffmpeg %d, errno: %d, error: %s\n", last_child, errno, strerror(errno));
                            return 8;
                        }
                        if (waited != last_child) {
                            fprintf(stderr, "Unexpected waited killed last child: want %d but get %d\n", last_child, waited);
                            return 9;
                        }
                        break;
                    default:
                        if (waited != last_child) {
                            fprintf(stderr, "Unexpected waited last child: want %d but get %d\n", last_child, waited);
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

void free_camera(struct Camera *camera) {
    if (camera->next) {
        free_camera(camera->next);
    }
    if (camera->recorder) {
        kill(camera->recorder, SIGINT);
        int status;
        waitpid(camera->recorder, &status, 0);
        fprintf(stderr, "Killed a camera recorder %d with return value %d\n", camera->recorder, status);
    }
    free(camera);
}

void free_storage(struct Storage *storage) {
    if (storage->next) {
        free_storage(storage->next);
    }
    if (storage->watcher) {
        kill(storage->watcher, SIGINT);
        int status;
        waitpid(storage->watcher, &status, 0);
        fprintf(stderr, "Killed a storage watcher %d with return value %d\n", storage->watcher, status);
    }
    free(storage);
}

enum arg_type {
    ILLEGAL,
    CAMERA,
    STORAGE
};

int main(int const argc, char *const argv[]) {
    struct Camera *cameras = NULL;
    struct Camera *camera_last = NULL;
    unsigned short camera_count = 0;
    struct Storage *storages = NULL;
    struct Storage *storage_last = NULL;
    unsigned short storage_count = 0;
    enum arg_type arg_type;
    int ret;
    for (int i = 1; i < argc; ++i) {
        arg_type = ILLEGAL;
        if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2]) {
            if (!strncmp(argv[i] + 2, "help", 4)) {
                help();
                ret = 0;
                goto free_all;
            }
            if (!strncmp(argv[i] + 2, "version", 7)) {
                version();
                ret = 0;
                goto free_all;
            }
            if (!strncmp(argv[i] + 2, "camera", 6)) {
                arg_type = CAMERA;
            }
            if (!strncmp(argv[i] + 2, "storage", 7)) {
                arg_type = STORAGE;
            }
        }
        switch (arg_type) {
            case ILLEGAL:
                fprintf(stderr, "Illegal argument: %s\n", argv[i]);
                help();
                ret = 1;
                goto free_all;
            case CAMERA:
            case STORAGE:
                if (i == argc - 1) {
                    fprintf(stderr, "Argument incomplete: %s\n", argv[i]);
                    help();
                    ret = 2;
                    goto free_all;
                }
                if (arg_type == CAMERA) {
                    struct Camera *camera_current = parse_camera(argv[i + 1]);
                    if (!camera_current) {
                        fprintf(stderr, "Error occured when trying to parse camera definition %s\n", argv[i + 1]);
                        ret = 3;
                        goto free_all;
                    }
                    if (cameras) {
                        camera_last->next = camera_current;
                    } else {
                        cameras = camera_current;
                    }
                    camera_last = camera_current;
                    ++camera_count;
                } else {
                    struct Storage *storage_current = parse_storage(argv[i + 1]);
                    if (!storage_current) {
                        fprintf(stderr, "Error occured when trying to parse storage definition %s\n", argv[i + 1]);
                        ret = 4;
                        goto free_all;
                    }
                    if (storages) {
                        storage_last->next = storage_current;
                    } else {
                        storages = storage_current;
                    }
                    storage_last = storage_current;
                    ++storage_count;
                }
                ++i;
                break;
        }
    }
    if (!cameras || !storages) {
        fprintf(stderr, "Either camera or storage not set, refuse to work. Camera: %s, Storage: %s\n", cameras ? "yes" : "no", storages ? "yes" : "no");
        help();
        ret = 5;
        goto free_all;
    }
    // pid_t *children = malloc(sizeof *children * (camera_count + storage_count));
    // if (!children) {
    //     ret = 6;
    //     goto free_all;
    // }
    // unsigned child_id = 0;
    for (struct Camera *camera = cameras; camera; camera = camera->next) {
        complete_camera(camera, storages);
    }
    printf("Working with %hu storages:\n", storage_count);
    for (struct Storage *storage = storages; storage; storage = storage->next) {
        printf("  %s clean threshods from %hu to %hu\n", storage->name, storage->threshold.from, storage->threshold.to);
        pid_t child = fork();
        switch (child) {
            case -1:
                fprintf(stderr, "Failed to fork storage watcher for %s, errno: %d, error: %s\n", storage->name, errno, strerror(errno));
                ret = 6;
                goto free_all;
            case 0: {
                int r = common_watcher(storage);
                if (r) {
                    fprintf(stderr, "Storage watcher for %s exited with %d\n", storage->name, r);
                    return 100 + r;
                }
                printf("Storage watcher for %s exited cleanly\n", storage->name);
                return 0;
            }
            default:
                storage->watcher = child;
                // children[child_id++] = child;
                break;
        }
    }
    printf("Working with %hu cameras:\n", camera_count);
    for (struct Camera *camera = cameras; camera; camera = camera->next) {
        printf("  %s from %s strftime %s\n", camera->name, camera->url, camera->strftime);
        pid_t child = fork();
        switch (child) {
            case -1:
                fprintf(stderr, "Failed to fork camera recorder for %s, errno: %d, error: %s\n", camera->name, errno, strerror(errno));
                ret = 7;
                goto free_all;
            case 0: {
                int r = camera_recorder(camera);
                if (r) {
                    fprintf(stderr, "Camera recorder for %s exited with %d\n", camera->name, r);
                    return 200 + r;
                }
                printf("Camera recorder for %s exited cleanly\n", camera->name);
                return 0;
            }
            default:
                camera->recorder = child;
                // children[child_id++] = child;
                break;
        }
    }

    while (true) {
        for (struct Storage *storage = storages; storage; storage = storage->next) {
            int status;
            pid_t waited = waitpid(storage->watcher, &status, WNOHANG);
            switch (waited) {
                case -1:
                    fprintf(stderr, "Failed to waitpid for storage watcher %d for %s, errno: %d, error: %s\n", storage->watcher, storage->name, errno, strerror(errno));
                    return 8;
                    goto free_all;
                case 0:
                    break;
                default:
                    fprintf(stderr, "Storage watcher %d for %s exited with %d which is not supposed\n", storage->watcher, storage->name, status);
                    storage->watcher = 0;
                    return 9;
                    goto free_all;

            }
        }
        for (struct Camera *camera = cameras; camera; camera = camera->next) {
            int status;
            pid_t waited = waitpid(camera->recorder, &status, WNOHANG);
            switch (waited) {
                case -1:
                    fprintf(stderr, "Failed to waitpid for camera recorder %d for %s, errno: %d, error: %s\n", camera->recorder, camera->name, errno, strerror(errno));
                    return 10;
                    goto free_all;
                case 0:
                    break;
                default:
                    fprintf(stderr, "Camera %d for %s exited with %d which is not supposed\n", camera->recorder, camera->name, status);
                    camera->recorder = 0;
                    return 11;
                    goto free_all;
            }

        }
        sleep(10);
    }

free_all:
    fprintf(stderr, "Cleaning up before quiting...\n");
    if (cameras) {
        free_camera(cameras);
    }
    if (storages) {
        free_storage(storages);
    }
    return ret;
};