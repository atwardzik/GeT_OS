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

static struct Line *visual_editor_get_current_line(struct VisualEditor *editor) {
        const int current_line_index = editor->cursor.row - editor->top_line_number;
        if (current_line_index < 0) {
                return nullptr;
        }

        return editor->lines[current_line_index];
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
        const int current_screen_index = editor->cursor.row - editor->top_line_number;
        int i = editor->lines_size - 1;

        while (i >= current_screen_index && i >= 0) {
                const int prev_index = i - 1;
                if (prev_index == current_screen_index) {
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

        const struct Line *line = visual_editor_get_current_line(editor);
        const size_t line_len = strlen(line->line);
        if (editor->cursor.col > line_len) {
                editor->cursor.col = line_len > 1 ? line_len - 1 : 1;
        }

        reprint_status_bar(editor);
        screen_move_absolute(next_line_index + 1, editor->cursor.col + line_number_field_length);
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

        const struct Line *line = visual_editor_get_current_line(editor);
        const size_t line_len = strlen(line->line);
        if (editor->cursor.col > line_len) {
                editor->cursor.col = line_len > 1 ? line_len - 1 : 1;
        }

        reprint_status_bar(editor);
        screen_move_absolute(previous_line_index + 1, editor->cursor.col + line_number_field_length);
}


static void visual_editor_move_left(struct VisualEditor *editor) {
        if (editor->cursor.col - 1 == 0) {
                if (cursor_can_move_up(editor)) {
                        visual_editor_move_up(editor);

                        const struct Line *line = visual_editor_get_current_line(editor);
                        const size_t line_len = strlen(line->line);
                        editor->cursor.col = line_len > 1 ? line_len - 1 : 1;
                }
        }
        else {
                editor->cursor.col -= 1;
        }
        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
}

static void visual_editor_move_right(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);
        const size_t line_len = strlen(line->line);
        if (editor->cursor.col + 1 >= line_len) {
                if (cursor_can_move_dn(editor)) {
                        visual_editor_move_dn(editor);

                        editor->cursor.col = 1;
                }
        }
        else {
                editor->cursor.col += 1;
        }
        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
}

static void visual_editor_move_home(struct VisualEditor *editor) {
        const unsigned int first_line_number = editor->top_line_number;

        editor->cursor.row = first_line_number;
        editor->cursor.col = 1;

        reprint_status_bar(editor);
        screen_move_absolute(1, line_number_field_length + 1);
}

static void visual_editor_move_middle(struct VisualEditor *editor) {
        const unsigned int middle_line_number =
                editor->top_line_number + editor->lines_size / 2;

        editor->cursor.row = middle_line_number;
        editor->cursor.col = 1;

        reprint_status_bar(editor);
        screen_move_absolute(editor->lines_size / 2, line_number_field_length + 1);
}

static void visual_editor_move_end(struct VisualEditor *editor) {
        const unsigned int last_line_number = editor->top_line_number + editor->lines_size - 1;

        editor->cursor.row = last_line_number;
        editor->cursor.col = 1;

        reprint_status_bar(editor);
        screen_move_absolute(editor->lines_size, line_number_field_length + 1);
}

static void visual_editor_move_current_line_begin(struct VisualEditor *editor) {
        editor->cursor.col = 1;
        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1, line_number_field_length + 1);
}

static void visual_editor_move_current_line_end(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);
        const size_t line_len = strlen(line->line);

        editor->cursor.col = line_len > 1 ? line_len - 1 : 1;

        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
}

static void visual_editor_move_first_line_word(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);

        editor->cursor.col = line->line[0] != '\n' ? strspn(line->line, "\t ") + 1 : 1;

        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
}

/*
 *							*word*
 * A word consists of a sequence of letters, digits and underscores, or a
 * sequence of other non-blank characters, separated with white space (spaces,
 * tabs, <EOL>).  This can be changed with the 'iskeyword' option.  For
 * characters above 255, a word ends when the Unicode character class changes
 * (e.g., between letters, subscripts, emojis, etc).  An empty line is also
 * considered to be a word.
 *                                                         *WORD*
 * A WORD consists of a sequence of non-blank characters, separated with white
 * space.  An empty line is also considered to be a WORD.
 */

static void visual_editor_move_next_WORD(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);
        size_t line_len = strlen(line->line);

        editor->cursor.col += strcspn(line->line + editor->cursor.col - 1, "\n\t ");
        editor->cursor.col += strspn(line->line + editor->cursor.col - 1, "\n\t ");

        if (line->line[0] == '\n' ||
            line->line[editor->cursor.col - 1] == '\n' ||
            editor->cursor.col >= strlen(line->line)) {
                editor->cursor.row += 1;
                editor->cursor.col = 1;

                line = visual_editor_get_current_line(editor);
                line_len = strlen(line->line);

                const int empty_chars_len = strspn(line->line + editor->cursor.col - 1, "\n\t ");
                editor->cursor.col = line_len > 1 ? empty_chars_len + 1 : 1;
        }

        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
}

static bool is_keyword(const char c) {
        return isalnum(c) || c == '_';
}

static bool is_blank(const char c) {
        return c == ' ' || c == '\n' || c == '\t';
}

static void visual_editor_move_next_word(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);
        size_t line_len = strlen(line->line);

        size_t i = editor->cursor.col - 1;

        if (i >= line_len) {
                return;
        }

        if (is_keyword(line->line[i])) {
                while (i < line_len && is_keyword(line->line[i])) {
                        i += 1;
                }
        }
        else if (!is_blank(line->line[i])) {
                while (i < line_len && !is_blank(line->line[i]) && !is_keyword(line->line[i])) {
                        i += 1;
                }
        }

        i += strspn(line->line + i, "\n\t ");

        if (line->line[0] == '\n' || i >= line_len) {
                editor->cursor.row += 1;
                editor->cursor.col = 1;
                line = visual_editor_get_current_line(editor);
                line_len = strlen(line->line);

                const int empty_chars_len = strspn(line->line + editor->cursor.col - 1, "\n\t ");
                editor->cursor.col = line_len > 1 ? empty_chars_len + 1 : 1;
        }
        else {
                editor->cursor.col = i + 1;
        }

        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
}

static void visual_editor_move_previous_word(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);

        // start from current index - 1, go as long as there are printable characters or begin of line
        // if in end of line, last character
        int current_index = editor->cursor.col - 1;

        reprint_status_bar(editor);
        screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                             editor->cursor.col + line_number_field_length);
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
        visual_editor_move_home(editor);
        printf("\x1b[1 q");

        bool echo = false, canonical = false;
        ioctl(0, TTY_ECHO, &echo);
        ioctl(0, TTY_CANONICAL, &canonical);
        char c;
        while (read(0, &c, 1) > 0) {
                switch (c) {
                        case '0':
                                visual_editor_move_current_line_begin(editor);
                                break;
                        case '$':
                                visual_editor_move_current_line_end(editor);
                                break;
                        case '_':
                                visual_editor_move_first_line_word(editor);
                                break;
                        case 'w':
                                visual_editor_move_next_word(editor);
                                break;
                        case 'W':
                                visual_editor_move_next_WORD(editor);
                                break;
                        case 'b':
                                visual_editor_move_previous_word(editor);
                                break;
                        case 'h':
                                visual_editor_move_left(editor);
                                break;
                        case 'i': {
                                editor->current_mode = INSERT;
                                reprint_status_bar(editor);
                                screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                                                     editor->cursor.col + line_number_field_length);
                                printf("\x1b[4 q");
                        }
                        break;
                        case 'j':
                                visual_editor_move_dn(editor);
                                break;
                        case 'k':
                                visual_editor_move_up(editor);
                                break;
                        case 'l':
                                visual_editor_move_right(editor);
                                break;
                        case 'H':
                                visual_editor_move_home(editor);
                                break;
                        case 'M':
                                visual_editor_move_middle(editor);
                                break;
                        case 'L':
                                visual_editor_move_end(editor);
                                break;
                        case ':':
                                printf("\x1b[40;1H");
                                printf(":");
                                break;
                        case '\x1b':
                                editor->current_mode = NORMAL;
                                reprint_status_bar(editor);
                                screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                                                     editor->cursor.col + line_number_field_length);
                                printf("\x1b[1 q");
                        default:
                                break;
                }
        }

        return 0;
}
