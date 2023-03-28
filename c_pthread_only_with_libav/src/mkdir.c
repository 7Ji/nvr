#include "mkdir.h"

#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/stat.h>

#include "print.h"

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