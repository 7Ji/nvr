#ifndef __HAVE_MUX_H
#define __HAVE_MUX_H

#include "common.h"
#include <time.h>

int mux(char const *in_filename, char const *out_filename, time_t time_end);

#endif