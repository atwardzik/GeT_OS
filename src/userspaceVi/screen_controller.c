//
// Created by Artur Twardzik on 08/07/2026.
//

#include "screen_controller.h"

#include "libc.h"

void screen_move_home(void) {
        printf("\x1b[H");
}

void screen_move_end(void) {}

void screen_move_cmd_field(void) {}

void screen_move_absolute(const unsigned int x, const unsigned int y) {
        printf("\x1b[%i;%iH", x, y);
}

void screen_move_to_col(const unsigned int x) {
        printf("\x1b[%iG", x);
}

void screen_clear(void) {
        printf("\x1b[2J");
}

void screen_scroll_dir_up(void) {
        printf("\x1b[S");
}

void screen_scroll_dir_down(void) {
        printf("\x1b[T");
}

void highlight(const char *pattern) {}
