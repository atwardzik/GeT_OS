//
// Created by Artur Twardzik on 08/07/2026.
//

#ifndef OS_EDITOR_H
#define OS_EDITOR_H

struct Line {
        char *line;
        unsigned int linecap;
        unsigned int line_number;

        bool edited;
};

#endif //OS_EDITOR_H
