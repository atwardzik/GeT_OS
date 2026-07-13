//
// Created by Artur Twardzik on 08/07/2026.
//

#ifndef OS_EDITOR_H
#define OS_EDITOR_H

#include "libc.h"

struct Line {
        char *line;
        unsigned int linecap;
        unsigned int line_number;

        bool edited;
};

static inline void free_line(struct Line *line) {
        free(line->line);
        free(line);
}

enum Mode {
        NORMAL,
        INSERT,
        REPLACE,
        VISUAL,
        VLINE,
        VBLOCK,
};

#endif //OS_EDITOR_H
