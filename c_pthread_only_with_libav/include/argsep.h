#ifndef __HAVE_ARGSEP_H
#define __HAVE_ARGSEP_H

#include "common.h"

unsigned short parse_argument_seps(char const *arg, char const *seps[], unsigned short sep_max, char const **end);

#endif