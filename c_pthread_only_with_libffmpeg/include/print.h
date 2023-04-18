#ifndef __HAVE_PRINT_H
#define __HAVE_PRINT_H

#include "common.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>

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

#endif