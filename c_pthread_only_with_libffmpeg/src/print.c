#include "print.h"

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
};