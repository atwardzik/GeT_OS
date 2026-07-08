//
// Created by Artur Twardzik on 08/07/2026.
//

#include "screen_controller.h"

#include "libc.h"

void move_home(void) {}
void move_end(void) {}
void move_cmd_field(void) {}

void move_absolute(int x, int y) {
        printf("\x1b[%i;%iH", x, y);
}

void move_to_col(int x) {
        printf("\x1b[%iG", x);
}

void clear_screen(void) {
        printf("\x1b[2J");
}

static inline void cmd_scroll_up(void) {
        printf("\x1b[S");
}

static inline void cmd_scroll_down(void) {
        printf("\x1b[T");
}

void write_line(struct Line *line) {}
void highlight(const char *pattern) {}
