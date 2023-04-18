#ifndef __HAVE_MKDIR_H
#define __HAVE_MKDIR_H

#include "common.h"

#include <sys/types.h>

int mkdir_recursive(char const *path, mode_t mode);

int mkdir_recursive_only_parent(char const *path, mode_t mode);

#endif