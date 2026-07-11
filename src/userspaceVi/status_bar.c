//
// Created by Artur Twardzik on 10/07/2026.
//

#include "status_bar.h"
#include "visual_editor.h"

#include "libc.h"
#include "screen_controller.h"

static void print_mode_color(const enum Mode mode) {
        switch (mode) {
                case NORMAL:
                        printf("\x1b[30;102m");
                        break;
                case INSERT:
                        printf("\x1b[30;106m");
                        break;
                case REPLACE:
                        printf("\x1b[30;101m");
                        break;
                case VISUAL:
                case VLINE:
                case VBLOCK:
                        printf("\x1b[30;105m");
                        break;
                default:
                        break;
        }
}

static const char *get_mode_str(const enum Mode mode) {
        switch (mode) {
                case NORMAL:
                        return "NORMAL";
                case INSERT:
                        return "INSERT";
                case REPLACE:
                        return "REPLACE";
                case VISUAL:
                        return "VISUAL";
                case VLINE:
                        return "V-LINE";
                case VBLOCK:
                        return "V-BLOCK";
                default:
                        return "      ";
        }
}

void reprint_status_bar(const struct VisualEditor *editor) {
        static enum Mode current_mode;
        static const char *current_filename;
        static int current_pos_len = 0;
        char pos[32] = {};
        const int pos_len = snprintf(pos, 32, " %i:%i ", editor->cursor.row, editor->cursor.col);

        if (current_mode != editor->current_mode ||
            current_filename != editor->filename ||
            current_pos_len != pos_len) {
                screen_move_absolute(39, 1);

                const char *mode_str = get_mode_str(editor->current_mode);
                print_mode_color(editor->current_mode);
                printf(" %s ", get_mode_str(editor->current_mode));

                const size_t filename_len = strlen(editor->filename) + 2;
                printf("\x1b[37;107m %s \x1b[0m", editor->filename);

                printf("\x1b[37;100m");
                for (int i = 0; i < 79 - strlen(mode_str) - filename_len - 2 - pos_len; ++i) {
                        printf(" ");
                }

                current_mode = editor->current_mode;
                current_filename = editor->filename;
                current_pos_len = pos_len;
        }
        else {
                screen_move_absolute(39, 80 - pos_len);
        }

        print_mode_color(editor->current_mode);
        printf("%s", pos);

        printf("\x1b[0m");
}
