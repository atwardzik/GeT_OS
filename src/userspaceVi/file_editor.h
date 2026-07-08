//
// Created by Artur Twardzik on 08/07/2026.
//

#ifndef OS_FILE_EDITOR_H
#define OS_FILE_EDITOR_H

#include <stddef.h>

#include "editor.h"

struct FileEditor;

struct FileEditor *create_file_editor(int fd);

void free_file_editor(struct FileEditor **editor);

int save_line(const struct FileEditor *editor, struct Line *line);

struct Line *get_line(const struct FileEditor *editor, unsigned int line_number);

struct Line *new_line_at(struct FileEditor *editor, int line_number);

#endif //OS_FILE_EDITOR_H
