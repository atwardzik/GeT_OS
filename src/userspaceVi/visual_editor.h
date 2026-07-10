//
// Created by Artur Twardzik on 08/07/2026.
//

#ifndef OS_VISUAL_EDITOR_H
#define OS_VISUAL_EDITOR_H

#include "editor.h"
#include "file_editor.h"

struct Cursor {
        unsigned int row;
        unsigned int col;
};

struct VisualEditor {
        const char *filename;
        const struct FileEditor *file_editor;

        unsigned int top_line_number;

        struct Line **lines;
        unsigned int lines_size;

        const struct Line *current_line;

        enum Mode current_mode;

        struct Line *cmd_line;

        struct Cursor cursor;
};

int run_editor(int argc, char **argv);

#endif //OS_VISUAL_EDITOR_H
