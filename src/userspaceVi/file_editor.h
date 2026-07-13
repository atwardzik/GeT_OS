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

int get_file_line(const struct FileEditor *editor, unsigned int line_number, struct Line *line);

int get_file_lines(struct FileEditor *editor, unsigned int line_number, unsigned int line_count, struct Line **lines);

struct Line *new_line_at(struct FileEditor *editor, int line_number);

int save_line(const struct FileEditor *editor, struct Line *line);

bool check_line_exists(const struct FileEditor *editor, unsigned int line_number);

#endif //OS_FILE_EDITOR_H
