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
/* GLIBC extension */
#include <getopt.h>
/* Linux */
#include <linux/limits.h>
#include <sys/sendfile.h>

/* Definitions */

#define STORAGE_NAME_MAXLEN     NAME_MAX+1
#define CAMERA_NAME_MAXLEN      NAME_MAX / 2
#define CAMERA_URL_MAXLEN       PATH_MAX
#define CAMERA_STRFTIME_MAXLEN  PATH_MAX  
#define CAMERA_ALLOC_BASE       5
#define CAMERA_ALLOC_MULTIPLY   1.5

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
    __pid_t watcher;
};

/**
 * @brief a struct to represent all storages
*/
struct Storages {
    /**
     * @brief the hot stoarge, things cleaned from here will be moved to cold storage
    */
    struct Storage hot;
    /**
     * @brief the cold storage, things cleaned from here will be simply deleted
    */
    struct Storage cold;
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
   __pid_t recorder;
};

/**
 * @brief a struct to hold all cameras
*/
struct Cameras {
    /**
     * @brief count of cameras, actual, could not be larger than alloc
    */
    unsigned short count;
    /**
     * @brief count of cameras that have been allocated memory for, could not be smaller than count
    */
    unsigned short alloc;
    /**
     * @brief all struct Camera members
    */
    struct Camera *members;
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
    puts("No help message available yet");
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

/**
 * @brief parse the input string to a struct Camera
 * 
 * @param camera pointer to the struct Camaera that should be filled
 * @param hot pointer the struct Storage hot that will be used to get 
 *            the hot storage folder name, to be used to generate the
 *            camera's strftime formatter
 * @param string the source string that should be parsed
 * 
 * @returns 0 for success, non-0 for failure
*/
int parse_camara(struct Camera *const camera, struct Storage const *const hot, char const *const string) {
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
        return 1;
    }
    snprintf(camera->strftime, CAMERA_STRFTIME_MAXLEN, "%s/%s_%%Y%%m%%d_%%H%%M%%S.mkv", hot->name, camera->name);
    printf("Camera added with name '%s', url '%s' and strftime '%s'\n", camera->name, camera->url, camera->strftime);
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

int common_watcher(struct Storage const *const hot, struct Storage const *const cold) {
    struct Space space = {0};
    DIR *dir;
    if (watcher_init(&space, &dir, hot)) {
        fprintf(stderr, "Failed to begin watching for folder '%s'\n", hot->name);
        return 1;
    }
    char path_old[PATH_MAX];
    char path_new[PATH_MAX];
    char *name_old = stpncpy(path_old, hot->name, NAME_MAX);
    // size_t len_dir_old = name_old - path_old;
    char *name_new = NULL;
    // size_t len_dir_new = 0;
    if (cold) {
        name_new = stpncpy(path_new, cold->name, NAME_MAX);
        // len_dir_new = name_new - path_new;
        *(name_new++) = '/';
    }
    *(name_old++) = '/';
    unsigned short cleaned;
    while (true) {
        if (update_space_further(&space, hot->name)) {
            fprintf(stderr, "Failed to update disk space for folder '%s'\n", hot->name);
            closedir(dir);
            return 2;
        }
        if (space.free <= space.from) {
            for (cleaned = 0; cleaned < 100; ++cleaned) { // Limit 100, to avoid endless loop
                if (get_oldest(path_old, name_old, dir)) {
                    fprintf(stderr, "Failed to get oldest file in folder '%s'\n", hot->name);
                    closedir(dir);
                    return 3;
                }
                rewinddir(dir);
                if (!name_old[0]) {
                    /* No oldest file found */
                    break;
                }
                // snprintf(path_old, PATH_MAX, "%s/%s", hot->name, oldest);
                if (cold) {
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
                if (update_space_further(&space, hot->name)) {
                    fprintf(stderr, "Failed to update disk space for folder '%s' when cleaning\n", hot->name);
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
    __pid_t child = 0;
    __pid_t last_child = 0;
    __pid_t waited;
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
                        if ((waited = waitpid(last_child, &status, 0))) {
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
        break;
    }
    return 0;
}

static inline int hot_watcher(struct Storage const *const hot, struct Storage const *const cold) {
    return common_watcher(hot, cold);
}

static inline int cold_watcher(struct Storage const *const cold) {
    return common_watcher(cold, NULL);
}

int main(int const argc, char *const argv[]) {
    struct Storages storages = {
        {"hot",  {10, 90}, 0},
        {"archived", {5, 10}, 0}
    };
    int c, option_index = 0;
    struct option const long_options[] = {
        {"cold",            required_argument,  NULL,   'c'},
        {"hot",             required_argument,  NULL,   'H'},
        {"help",            no_argument,        NULL,   'h'},
        {"threshold-hot",   required_argument,  NULL,   'T'},
        {"threshold-cold",  required_argument,  NULL,   't'},
        {"version",         no_argument,        NULL,   'v'},
        {NULL,              0,                  NULL,   '\0'},
    };
    while ((c = getopt_long(argc, argv, "c:hH:T:t:v", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                if (safe_strncpy(storages.cold.name, optarg, STORAGE_NAME_MAXLEN)) {
                    fprintf(stderr, "Failed to copy string as cold storage name: %s\n", optarg);
                    return 1;
                }
                break;
            case 'H':
                if (safe_strncpy(storages.hot.name, optarg, STORAGE_NAME_MAXLEN)) {
                    fprintf(stderr, "Failed to copy string as hot storage name: %s\n", optarg);
                    return 2;
                }
                break;
            case 'h':
                help();
                return 0;
            case 'T':
                if (parse_threshold(&storages.hot.threshold, optarg)) {
                    fprintf(stderr, "Failed to parse threshold for hot storage: %s\n", optarg);
                    return 3;
                }
                break;
            case 't':
                if (parse_threshold(&storages.cold.threshold, optarg)) {
                    fprintf(stderr, "Failed to parse threshold for cold storage: %s\n", optarg);
                    return 4;
                }
                break;
            case 'v':
                version();
                return 0;
            default:
                fprintf(stderr, "Unrecognizable option %s\n", argv[optind-1]);
                return 1;
        }
    }
    printf("Using '%s' as hot storage (threshold: from %hu%% to %hu%%) and '%s' as cold storage (threshold: from %u%% to %u%%) \n", storages.hot.name, storages.hot.threshold.from, storages.hot.threshold.to, storages.cold.name, storages.cold.threshold.from, storages.cold.threshold.to);
    struct Cameras cameras = {0, 0, NULL};
    struct Camera *camera_p;
    for (int i = optind; i < argc; ++i) {
        if (++cameras.count > cameras.alloc) {
            if (cameras.alloc) {
                cameras.alloc *= CAMERA_ALLOC_MULTIPLY;
                if ((camera_p = realloc(cameras.members, cameras.alloc * sizeof *cameras.members))) {
                    printf("Realloc cameras to %hu since count increased to %hu\n", cameras.alloc, cameras.count);
                } else {
                    free(cameras.members);
                    fprintf(stderr, "Failed to resize cameras to %hu\n", cameras.alloc);
                    return 5;
                }
            } else {
                if ((camera_p = malloc(CAMERA_ALLOC_BASE * sizeof *cameras.members))) {
                    cameras.alloc = CAMERA_ALLOC_BASE;
                } else {
                    fprintf(stderr, "Failed to allocate memory for %hu cameras\n", CAMERA_ALLOC_BASE);
                    return 6;
                }
            }
            cameras.members = camera_p;
        }
        camera_p = cameras.members + cameras.count - 1;
        if (parse_camara(camera_p, &storages.hot, argv[i])) {
            fprintf(stderr, "Failed to parse camera no %hu: %s\n", cameras.count, argv[i]);
            return 7;
        }
        for (int j = 0; j < cameras.count - 1; ++j) {
            if (!strncmp((cameras.members + j)->name, camera_p->name, CAMERA_NAME_MAXLEN)) {
                fprintf(stderr, "Camera name duplicated: %s\n", camera_p->name);
                return 8;
            }
        }
    }
    if (!cameras.count) {
        fputs("No cameras defined\n", stderr);
        return 12;
    }
    __pid_t child;
    int r;
    switch (child = fork()) {
        case -1:
            fprintf(stderr, "Failed to fork cold storage watcher, errno: %d, error: %s\n", errno, strerror(errno));
            return 9;
        case 0:
            if ((r = cold_watcher(&storages.cold))) {
                fprintf(stderr, "Cold storage watcher: bad things happended, return %d\n", r);
                return 100 + r;
            } else {
                return 0;
            }
        default:
            printf("Forked child %d as cold storage watcher\n", child);
            storages.cold.watcher = child;
            break;
    }
    switch (child = fork()) {
        case -1:
            fprintf(stderr, "Failed to fork hot storage watcher, errno: %d, error: %s\n", errno, strerror(errno));
            return 10;
        case 0:
            if ((r = hot_watcher(&storages.hot, &storages.cold))) {
                fprintf(stderr, "Hot storage watcher: bad things happended, return %d\n", r);
                return 200 + r;
            } else {
                return 0;
            }
        default:
            printf("Forked child %d as hot storage watcher\n", child);
            storages.hot.watcher = child;
            break;
    }
    for (int i = 0; i < cameras.count; ++i) {
        struct Camera *camera = cameras.members + i;
        switch (child = fork()) {
            case -1:
                fprintf(stderr, "Failed to fork camera recorder, errno: %d, error: %s\n", errno, strerror(errno));
                for (int j = 0; j < i; ++i) {
                    kill(cameras.members[j].recorder, SIGINT);
                }
                free(cameras.members);
                return 11;
            case 0:
                if ((r = camera_recorder(camera))) {
                    fprintf(stderr, "Camera recorder: bad things happended, return %d\n", r);
                    return 300 + r;
                } else {
                    return 0;
                }
            default:
                printf("Forked child %d as camera recorder\n", child);
                camera->recorder = child;
                break;
        }
    }
    unsigned short children_count = cameras.count + 2;
    pid_t *children = malloc(children_count * sizeof *children);
    if (!children) {
        fprintf(stderr, "Failed to allocate memory for all children to further checkup, errno: %d, error: %s\n", errno, strerror(errno));
        return 12;
    }
    for (unsigned short i = 0; i < cameras.count; ++i) {
        children[i] = cameras.members[i].recorder;
    }
    children[cameras.count] = storages.hot.watcher;
    children[cameras.count + 1] = storages.cold.watcher;
    int status;
    pid_t waited;
    while (true) {
        for (unsigned short i = 0; i < children_count; ++i) {
            if ((waited = waitpid(children[i], &status, WNOHANG))) {
                if (waited == -1) {
                    fprintf(stderr, "Failed to waitpid for child %d, errno: %d, error: %s\n", children[i], errno, strerror(errno));
                } else {
                    fprintf(stderr, "Child %d exited, status %d\n", children[i], status);
                }
                fputs("Sending SIGINT to all children before quiting\n", stderr);
                for (unsigned short j = 0; j < children_count; ++j) {
                    if (waited != -1 && i == j) {
                        continue;
                    }
                    kill(children[j], SIGINT);
                    waitpid(children[j], &status, 0);
                }
                break;
            }
        }
        sleep(10);
    }
    free(children);
    free(cameras.members);
    return 12;
};