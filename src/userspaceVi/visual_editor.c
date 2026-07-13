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

static struct Line *visual_editor_get_current_line(const struct VisualEditor *editor) {
        const int current_line_index = editor->cursor.row - editor->top_line_number;
        if (current_line_index < 0) {
                return nullptr;
        }

        return editor->lines[current_line_index];
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


static void visual_editor_scroll_dir_up(struct VisualEditor *editor) {
        save_line(editor->file_editor, editor->lines[0]);

        unsigned int i = 0;
        while (i <= editor->cursor.row) {
                const unsigned int prev_index = i + 1;
                if (prev_index > editor->lines_size - 1) {
                        break;
                }

                editor->lines[i] = editor->lines[prev_index];
                i += 1;
        }
        editor->top_line_number += 1;

        screen_scroll_dir_up();

        struct Line *line = get_line(editor->file_editor, editor->cursor.row);
        const int line_screen_index = editor->cursor.row - editor->top_line_number;
        editor->lines[line_screen_index] = line;
        print_line(editor, line, false);
}

static void visual_editor_scroll_dir_dn(struct VisualEditor *editor) {
        save_line(editor->file_editor, editor->lines[editor->lines_size - 1]);

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
        editor->top_line_number -= 1;

        screen_scroll_dir_dn();

        struct Line *line = get_line(editor->file_editor, editor->cursor.row);
        const int line_screen_index = editor->cursor.row - editor->top_line_number;
        editor->lines[line_screen_index] = line;
        print_line(editor, line, false);
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

static struct Cursor visual_editor_get_move_dn(const struct VisualEditor *editor) {
        if (!cursor_can_move_dn(editor)) {
                return editor->cursor;
        }

        struct Cursor cursor = editor->cursor;
        cursor.row += 1;

        struct Line *line = get_line(editor->file_editor, cursor.row);
        const size_t line_len = strlen(line->line);
        if (cursor.col > line_len) {
                cursor.col = line_len > 1 ? line_len - 1 : 1;
        }
        free_line(line);

        return cursor;
}

static struct Cursor visual_editor_get_move_up(const struct VisualEditor *editor) {
        if (!cursor_can_move_up(editor)) {
                return editor->cursor;
        }

        struct Cursor cursor = editor->cursor;
        cursor.row -= 1;

        struct Line *line = get_line(editor->file_editor, cursor.row);
        const size_t line_len = strlen(line->line);
        if (cursor.col > line_len) {
                cursor.col = line_len > 1 ? line_len - 1 : 1;
        }
        free_line(line);

        return cursor;
}


static struct Cursor visual_editor_get_move_left(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        if (cursor.col - 1 == 0) {
                if (cursor_can_move_up(editor)) {
                        cursor.row -= 1;

                        struct Line *line = get_line(editor->file_editor, cursor.row);
                        const size_t line_len = strlen(line->line);

                        cursor.col = line_len > 1 ? line_len - 1 : 1;
                        free_line(line);
                }
        }
        else {
                cursor.col -= 1;
        }

        return cursor;
}

static struct Cursor visual_editor_get_move_right(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        const struct Line *line = visual_editor_get_current_line(editor);
        const size_t line_len = strlen(line->line);
        if (cursor.col + 1 >= line_len) {
                if (cursor_can_move_dn(editor)) {
                        cursor.row += 1;
                        cursor.col = 1;
                }
        }
        else {
                cursor.col += 1;
        }

        return cursor;
}

static struct Cursor visual_editor_get_move_home(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        cursor.row = editor->top_line_number;
        cursor.col = 1;

        return cursor;
}

static struct Cursor visual_editor_get_move_middle(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        cursor.row = editor->top_line_number + editor->lines_size / 2;
        cursor.col = 1;

        return cursor;
}

static struct Cursor visual_editor_get_move_end(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        cursor.row = editor->top_line_number + editor->lines_size - 1;
        cursor.col = 1;

        return cursor;
}

static struct Cursor visual_editor_get_move_current_line_begin(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        cursor.col = 1;

        return cursor;
}

static struct Cursor visual_editor_get_move_current_line_end(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        const struct Line *line = visual_editor_get_current_line(editor);
        const size_t line_len = strlen(line->line);

        cursor.col = line_len > 1 ? line_len - 1 : 1;

        return cursor;
}

static struct Cursor visual_editor_get_move_first_line_word(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        const struct Line *line = visual_editor_get_current_line(editor);

        cursor.col = line->line[0] != '\n' ? strspn(line->line, "\t ") + 1 : 1;

        return cursor;
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

static struct Cursor visual_editor_get_move_next_WORD(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        struct Line *line = visual_editor_get_current_line(editor);
        size_t line_len = strlen(line->line);

        cursor.col += strcspn(line->line + cursor.col - 1, "\n\t ");
        cursor.col += strspn(line->line + cursor.col - 1, "\n\t ");

        if (line->line[0] == '\n' || line->line[cursor.col - 1] == '\n' || editor->cursor.col >= line_len) {
                cursor.row += 1;
                cursor.col = 1;

                line = get_line(editor->file_editor, cursor.row);
                line_len = strlen(line->line);

                const int empty_chars_len = strspn(line->line + cursor.col - 1, "\n\t ");
                cursor.col = line_len > 1 ? empty_chars_len + 1 : 1;

                free_line(line);
        }

        return cursor;
}

static bool is_keyword(const char c) {
        return isalnum(c) || c == '_';
}

static bool is_blank(const char c) {
        return c == ' ' || c == '\n' || c == '\t';
}

static struct Cursor visual_editor_get_move_next_word(const struct VisualEditor *editor) {
        struct Cursor cursor = editor->cursor;

        struct Line *line = visual_editor_get_current_line(editor);
        size_t line_len = strlen(line->line);

        size_t i = editor->cursor.col - 1;

        if (i >= line_len) {
                return editor->cursor;
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
                cursor.row += 1;
                cursor.col = 1;

                line = get_line(editor->file_editor, cursor.row);
                line_len = strlen(line->line);

                const int empty_chars_len = strspn(line->line + cursor.col - 1, "\n\t ");
                cursor.col = line_len > 1 ? empty_chars_len + 1 : 1;

                free_line(line);
        }
        else {
                cursor.col = i + 1;
        }

        return cursor;
}

static struct Cursor visual_editor_get_move_previous_word(struct VisualEditor *editor) {
        const struct Line *line = visual_editor_get_current_line(editor);

        // start from current index - 1, go as long as there are printable characters or begin of line
        // if in end of line, last character
        int current_index = editor->cursor.col - 1;
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
        visual_editor_get_move_home(editor);
        screen_move_absolute(1, 1 + line_number_field_length);
        printf("\x1b[1 q");

        bool echo = false, canonical = false;
        ioctl(0, TTY_ECHO, &echo);
        ioctl(0, TTY_CANONICAL, &canonical);
        char c;
        while (read(0, &c, 1) > 0) {
                //below are editor moves only
                struct Cursor cursor_new = editor->cursor;
                switch (c) {
                        case '0':
                                cursor_new = visual_editor_get_move_current_line_begin(editor);
                                break;
                        case '$':
                                cursor_new = visual_editor_get_move_current_line_end(editor);
                                break;
                        case '_':
                                cursor_new = visual_editor_get_move_first_line_word(editor);
                                break;
                        case 'w':
                                cursor_new = visual_editor_get_move_next_word(editor);
                                break;
                        case 'W':
                                cursor_new = visual_editor_get_move_next_WORD(editor);
                                break;
                        case 'b':
                                cursor_new = visual_editor_get_move_previous_word(editor);
                                break;
                        case 'h':
                                cursor_new = visual_editor_get_move_left(editor);
                                break;
                        case 'i':
                                editor->current_mode = INSERT;
                                printf("\x1b[4 q");
                                break;
                        case 'j':
                                cursor_new = visual_editor_get_move_dn(editor);
                                break;
                        case 'k':
                                cursor_new = visual_editor_get_move_up(editor);
                                break;
                        case 'l':
                                cursor_new = visual_editor_get_move_right(editor);
                                break;
                        case 'H':
                                cursor_new = visual_editor_get_move_home(editor);
                                break;
                        case 'M':
                                cursor_new = visual_editor_get_move_middle(editor);
                                break;
                        case 'L':
                                cursor_new = visual_editor_get_move_end(editor);
                                break;
                        case ':': //FIXME: should not be in this switch
                                printf("\x1b[40;1H");
                                printf(":");
                                break;
                        case '\x1b':
                                editor->current_mode = NORMAL;
                                printf("\x1b[1 q");
                                break;
                        default:
                                break;
                }

                editor->cursor = cursor_new;

                int next_line_index = cursor_new.row - editor->top_line_number;
                while (next_line_index < 0 || next_line_index >= editor->lines_size) {
                        if (next_line_index < 0) {
                                visual_editor_scroll_dir_dn(editor);
                        }
                        else {
                                visual_editor_scroll_dir_up(editor);
                        }
                        next_line_index = cursor_new.row - editor->top_line_number;
                }

                reprint_status_bar(editor);
                screen_move_absolute(editor->cursor.row - editor->top_line_number + 1,
                                     editor->cursor.col + line_number_field_length);
        }

        return 0;
}
