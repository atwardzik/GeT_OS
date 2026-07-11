//
// Created by Artur Twardzik on 08/07/2026.
//

#include "visual_editor.h"
#include "file_editor.h"
#include "screen_controller.h"

#include "errno.h"
#include "libc.h"
#include "status_bar.h"

static struct VisualEditor *create_visual_editor(const unsigned int lines_max, const char *filename) {
        struct VisualEditor *editor = malloc(sizeof(*editor));
        if (!editor) {
                return nullptr;
        }

        const int fd = open(filename, O_RDWR, 0);
        if (fd < 0) {
                dprintf(2, "[!] No such file.\n");
                free(editor->lines);
                free(editor);
                return nullptr;
        }

        struct FileEditor *file_editor = create_file_editor(fd);
        if (!file_editor) {
                free(editor);
                return nullptr;
        }

        editor->lines = malloc(sizeof(struct Line *) * lines_max);
        if (!editor->lines) {
                free(file_editor);
                free(editor);
                return nullptr;
        }
        for (int i = 0; i < lines_max; ++i) {
                editor->lines[i] = get_line(file_editor, i + 1);
        }

        editor->lines_size = lines_max;
        editor->top_line_number = 1;
        editor->cmd_line = nullptr;
        editor->file_editor = file_editor;
        editor->current_mode = NORMAL;
        editor->filename = filename;
        editor->cursor.row = 1;
        editor->cursor.col = 1;

        return editor;
}

static void free_visual_editor(struct VisualEditor **editor) {
        if (!*editor) {
                return;
        }

        if ((*editor)->lines) {
                for (int i = 0; i < (*editor)->lines_size; ++i) {
                        free((*editor)->lines[i]);
                }
        }
        free((*editor)->lines);
        free((*editor)->cmd_line);

        free(*editor);
}

static void visual_editor_scroll_dir_up(const struct VisualEditor *editor) {
        unsigned int i = 0;

        while (i <= editor->cursor.row) {
                const unsigned int prev_index = i + 1;
                if (prev_index > editor->lines_size - 1) {
                        break;
                }

                editor->lines[i] = editor->lines[prev_index];
                i += 1;
        }

        screen_scroll_dir_up();
}

static void visual_editor_scroll_dir_dn(const struct VisualEditor *editor) {
        int i = editor->lines_size - 1;

        while (i >= 0) {
                const int prev_index = i - 1;
                if (prev_index < editor->cursor.row - editor->top_line_number) {
                        editor->lines[i] = nullptr;
                }
                else {
                        editor->lines[i] = editor->lines[prev_index];
                }

                i -= 1;
        }

        screen_scroll_dir_dn();
}

static int determine_column_offset(unsigned int line_number) {
        int column_offset = 1;
        while (line_number) {
                column_offset += 1;
                line_number /= 10;
        }
        column_offset += 1;

        return column_offset;
}


static int line_number_field_length = 3;

static void print_line_number(const unsigned int line_number) {
        char line_num[10] = {}; //low chances the files opened will have 9 digit lines
        screen_move_to_col(1);
        const int line_num_length = snprintf(line_num, 10, "%i", line_number);
        printf("\x1b[90;49m%s\x1b[0m", line_num);

        for (int i = line_num_length; i < line_number_field_length; ++i) {
                printf(" ");
        }
}

static void reprint_screen(struct VisualEditor *editor);

static void print_line(struct VisualEditor *editor, const struct Line *line, const bool newline) {
        if (!line || !line->line || !strlen(line->line)) {
                return;
        }

        const int line_number_field_len = determine_column_offset(line->line_number) - 1;
        if (line_number_field_len > line_number_field_length) {
                line_number_field_length = line_number_field_len;
                reprint_screen(editor);
                return;
        }
        print_line_number(line->line_number);

        int len = strlen(line->line) - 1;
        len = len > 79 - line_number_field_length ? 79 - line_number_field_length : len;
        write(1, line->line, len);

        if (newline) {
                printf("\n");
        }
}

static void reprint_screen(struct VisualEditor *editor) {
        screen_move_home();

        int i = 0;
        for (i = 0; i < editor->lines_size - 1; ++i) {
                print_line(editor, editor->lines[i], true);
        }
        print_line(editor, editor->lines[i], false);

        screen_move_absolute(1, line_number_field_length + 1);
}

static bool cursor_can_move_dn(const struct VisualEditor *editor) {
        if (check_line_exists(editor->file_editor, editor->cursor.row + 1)) {
                return true;
        }

        return false;
}

static bool cursor_can_move_up(const struct VisualEditor *editor) {
        if (editor->cursor.row - 1 > 0) {
                return true;
        }

        return false;
}

static void visual_editor_move_dn(struct VisualEditor *editor) {
        if (!cursor_can_move_dn(editor)) {
                return;
        }

        editor->cursor.row += 1;

        unsigned int next_line_index = editor->cursor.row - editor->top_line_number;
        if (next_line_index > editor->lines_size - 1) {
                save_line(editor->file_editor, editor->lines[0]);

                visual_editor_scroll_dir_up(editor);

                struct Line *line = get_line(editor->file_editor, editor->cursor.row);
                editor->lines[editor->lines_size - 1] = line;

                print_line(editor, line, false);

                editor->top_line_number += 1;
                next_line_index -= 1;
        }

        reprint_status_bar(editor);
        screen_move_absolute(next_line_index + 1, line_number_field_length + 1);
}

static void visual_editor_move_up(struct VisualEditor *editor) {
        if (!cursor_can_move_up(editor)) {
                return;
        }

        editor->cursor.row -= 1;

        int previous_line_index = editor->cursor.row - editor->top_line_number;
        if (previous_line_index < 0) {
                save_line(editor->file_editor, editor->lines[editor->lines_size - 1]);

                visual_editor_scroll_dir_dn(editor);

                struct Line *line = get_line(editor->file_editor, editor->cursor.row);
                editor->lines[0] = line;

                print_line(editor, editor->lines[0], false);
                editor->top_line_number -= 1;
                previous_line_index += 1;
        }

        reprint_status_bar(editor);
        screen_move_absolute(previous_line_index + 1, line_number_field_length + 1);
}

int run_editor(int argc, char **argv) {
        if (argc < 2) {
                dprintf(2, "[!] Not enough parameters supplied.");
                return 1;
        }

        pid_t pgid = 1; //should this be done by the shell?
        setpgid(0, pgid);
        ioctl(0, TTY_SPGRP, &pgid);

        constexpr int lines_max = 38;
        struct VisualEditor *editor __attribute__((cleanup(free_visual_editor))) = create_visual_editor(
                lines_max, argv[1]);
        if (!editor) {
                dprintf(2, "[!] Could not instantiate visual editor.\n");
                return 1;
        }

        screen_clear();
        printf("\x1b[1;%ir", lines_max); //set scrollable content

        screen_move_home();
        reprint_screen(editor);
        reprint_status_bar(editor);
        screen_move_absolute(1, line_number_field_length + 1);

        bool echo = false, canonical = false;
        ioctl(0, TTY_ECHO, &echo);
        ioctl(0, TTY_CANONICAL, &canonical);
        char c;
        while (read(0, &c, 1) > 0) {
                switch (c) {
                        case 'h': {
                                //beware of line numbers, go to end of previous line if less than zero
                                editor->cursor.col -= 1;
                                //change cursor absolute position
                        }
                        break;
                        case 'i': {
                                editor->current_mode = INSERT;
                                reprint_status_bar(editor);
                        }
                        break;
                        case 'j':
                                visual_editor_move_dn(editor);
                                break;
                        case 'k':
                                visual_editor_move_up(editor);
                                break;
                        case 'l': {
                                //
                        }
                        break;
                        case 'H': {
                                const unsigned int first_line_number = editor->top_line_number;

                                editor->cursor.row = first_line_number;
                                editor->cursor.col = 1;

                                reprint_status_bar(editor);
                                screen_move_absolute(1, line_number_field_length + 1);
                        }
                        break;
                        case 'M': {
                                const unsigned int middle_line_number =
                                        editor->top_line_number + editor->lines_size / 2;

                                editor->cursor.row = middle_line_number;
                                editor->cursor.col = 1;

                                reprint_status_bar(editor);
                                screen_move_absolute(editor->lines_size / 2, line_number_field_length + 1);
                        }
                        break;
                        case 'L': {
                                const unsigned int last_line_number = editor->top_line_number + editor->lines_size - 1;

                                editor->cursor.row = last_line_number;
                                editor->cursor.col = 1;

                                reprint_status_bar(editor);
                                screen_move_absolute(editor->lines_size, line_number_field_length + 1);
                        }
                        break;
                        case ':':
                                printf("\x1b[40;1H");
                                printf(":");
                                break;
                        default:
                                break;
                }
        }

        return 0;
}
