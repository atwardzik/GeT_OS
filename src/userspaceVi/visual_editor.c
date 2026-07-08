//
// Created by Artur Twardzik on 08/07/2026.
//

#include "visual_editor.h"
#include "file_editor.h"
#include "screen_controller.h"

#include "errno.h"
#include "libc.h"

struct VisualEditor {
        unsigned int top_line_number;

        const struct FileEditor *file_editor;

        struct Line **lines;
        unsigned int lines_size;

        const struct Line *current_line;

        struct Line *cmd_line;
};

static struct VisualEditor *create_visual_editor(const unsigned int lines_max, const char *filename) {
        struct VisualEditor *editor = malloc(sizeof(*editor));
        if (!editor) {
                return nullptr;
        }

        editor->lines = malloc(sizeof(struct Line *) * lines_max);
        if (!editor->lines) {
                free(editor);
                return nullptr;
        }
        memset(editor->lines, 0, sizeof(struct Line *) * lines_max);

        const int fd = open(filename, O_RDONLY | O_WRONLY, 0);
        if (fd < 0) {
                dprintf(2, "[!] No such file.\n");
                free(editor->lines);
                free(editor);
                return nullptr;
        }

        struct FileEditor *file_editor = create_file_editor(fd);
        if (!file_editor) {
                dprintf(2, "[!] Could not instantiate file editor.\n");
                free(editor->lines);
                free(editor);
                return nullptr;
        }

        editor->lines_size = lines_max;
        editor->top_line_number = 1;
        editor->current_line = nullptr;
        editor->cmd_line = nullptr;
        editor->file_editor = file_editor;

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

void save_onscreen_lines(
        const struct VisualEditor *editor, const unsigned int screen_line_start, const unsigned int screen_line_end
) {
        for (unsigned int i = screen_line_start - 1; i <= screen_line_end - 1; ++i) {
                struct Line *line = editor->lines[i];
                if (line && line->edited) {
                        save_line(editor->file_editor, line);
                }
        }
}

void free_onscreen_lines(
        const struct VisualEditor *editor, const unsigned int screen_line_start, const unsigned int screen_line_end
) {
        for (unsigned int i = screen_line_start - 1; i <= screen_line_end - 1; ++i) {
                free(editor->lines[i]);
                editor->lines[i] = nullptr;
        }
}

enum ShiftDirection {
        DIR_UP,
        DIR_DN,
};

void shift_dir_up(
        const struct VisualEditor *editor, const unsigned int screen_line_start, const unsigned int shift_amount
) {
        unsigned int i = screen_line_start - 1;

        while (i <= editor->lines_size - 1) {
                const unsigned int prev_index = i + shift_amount;
                if (prev_index > editor->lines_size - 1) {
                        break;
                }

                editor->lines[i] = editor->lines[prev_index];
                i += 1;
        }
}

void shift_dir_dn(
        const struct VisualEditor *editor, const unsigned int screen_line_start, const unsigned int shift_amount
) {
        int i = editor->lines_size - 1;

        while (i >= 0) {
                const int prev_index = i - shift_amount;
                if (prev_index < screen_line_start - 1) {
                        editor->lines[i] = nullptr;
                }
                else {
                        editor->lines[i] = editor->lines[prev_index];
                }

                i -= 1;
        }
}

void shift(
        const struct VisualEditor *editor, const enum ShiftDirection shift_dir, const unsigned int screen_line_start,
        const unsigned int shift_amount
) {
        if (shift_dir == DIR_DN) {
                shift_dir_dn(editor, screen_line_start, shift_amount);
        }
        else {
                shift_dir_up(editor, screen_line_start, shift_amount);
        }
}

void fetch_whole_screen(const struct VisualEditor *editor, const unsigned int line_number_start) {
        save_onscreen_lines(editor, 1, editor->lines_size);

        free_onscreen_lines(editor, 1, editor->lines_size);

        for (unsigned int i = 0; i < editor->lines_size; ++i) {
                struct Line *line = get_line(editor->file_editor, line_number_start + i);
                editor->lines[i] = line;
        }
}

void fetch_screen_at(struct VisualEditor *editor, const unsigned int line_number_start) {
        const int line_difference = (int) editor->top_line_number - (int) line_number_start;
        if (line_difference == 0 || abs(line_difference) >= editor->lines_size) {
                fetch_whole_screen(editor, line_number_start);
                return;
        }
        const enum ShiftDirection shift_dir = line_difference > 0 ? DIR_DN : DIR_UP;

        unsigned int invalid_screen_lines_start = 0;
        unsigned int invalid_screen_lines_end = 0;
        if (shift_dir == DIR_DN) {
                invalid_screen_lines_start = editor->lines_size - abs(line_difference) + 1;
                invalid_screen_lines_end = editor->lines_size;
        }
        else if (shift_dir == DIR_UP) {
                invalid_screen_lines_start = 1;
                invalid_screen_lines_end = abs(line_difference);
        }


        save_onscreen_lines(editor, invalid_screen_lines_start, invalid_screen_lines_end);

        free_onscreen_lines(editor, invalid_screen_lines_start, invalid_screen_lines_end);

        shift(editor, shift_dir, 1, abs(line_difference));


        unsigned int destination_screen_line_start = 0;
        unsigned int destination_screen_line_end = 0;
        if (shift_dir == DIR_DN) {
                destination_screen_line_start = 1;
                destination_screen_line_end = line_difference;
        }
        else if (shift_dir == DIR_UP) {
                destination_screen_line_start = editor->lines_size - abs(line_difference) + 1;
                destination_screen_line_end = editor->lines_size;
        }

        for (unsigned int i = destination_screen_line_start; i <= destination_screen_line_end; ++i) {
                struct Line *line = get_line(editor->file_editor, line_number_start + i - 1);
                editor->lines[i - 1] = line;
        }

        editor->top_line_number += abs(line_difference);
}

static inline int determine_column_offset(int line_number) {
        int column_offset = 1;
        while (line_number) {
                column_offset += 1;
                line_number /= 10;
        }
        column_offset += 1;

        return column_offset;
}

int run_editor(int argc, char **argv) {
        if (argc < 2) {
                dprintf(2, "[!] Not enough parameters supplied.");
                return 1;
        }

        pid_t pgid = 1; //should this be done by the shell?
        setpgid(0, pgid);
        ioctl(0, TTY_SPGRP, &pgid);

        int lines_max = 39;
        struct VisualEditor *editor __attribute__((cleanup(free_visual_editor))) = create_visual_editor(
                lines_max, argv[1]);
        if (!editor) {
                dprintf(2, "[!] Could not instantiate visual editor.\n");
                return 1;
        }

        clear_screen();
        printf("\x1b[1;%ir", lines_max); //set scrollable contents
        fetch_screen_at(editor, 1);

        bool echo = false, canonical = false;
        ioctl(0, TTY_ECHO, &echo);
        ioctl(0, TTY_CANONICAL, &canonical);
        char c;
        while (read(0, &c, 1) > 0) {
                switch (c) {
                        case 'j': {
                                fetch_screen_at(editor, editor->top_line_number + 1);
                        }
                        break;
                        case 'k': {
                                fetch_screen_at(editor, editor->top_line_number - 1);
                        }
                        break;
                        case 'H': {
                                const int first_line_number = editor->top_line_number;
                                const int column_offset = determine_column_offset(first_line_number);

                                move_absolute(1, column_offset);
                        }
                        break;
                        case 'M':
                                printf("\x1b[%iH", (editor->lines_size - 1) / 2);
                                // jump_column_line_offset(editor);
                                break;
                        case 'L': {
                                const int last_line_number = editor->top_line_number + editor->lines_size - 1;
                                const int column_offset = determine_column_offset(last_line_number);

                                move_absolute(editor->lines_size, column_offset);
                        }
                        break;
                        case ':':
                                printf("\x1b[%iH", editor->lines_size + 1);
                                printf(":");
                                break;
                        default:
                                break;
                }
        }

        return 0;
}
