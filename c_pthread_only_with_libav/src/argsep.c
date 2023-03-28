#include "argsep.h"

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