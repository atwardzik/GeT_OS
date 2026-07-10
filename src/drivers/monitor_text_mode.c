//
// Created by Artur Twardzik on 01/07/2026.
//
#include "monitor_text_mode.h"

#include "drivers/vga.h"

#include "ascii_char_codes.h"
#include "config.h"

extern uint8_t __screen_buffer_start__[];
extern uint8_t __screen_buffer_length__[];

uint8_t *const screen_buffer_ptr = __screen_buffer_start__;
const uint8_t *const screen_length_ptr = __screen_buffer_length__;


struct SingleChar {
        uint8_t ascii_code;
        ByteColorCode color_code;
};

struct CharBuffer {
        struct SingleChar chars[BUFFER_HEIGHT][BUFFER_WIDTH];
};

static struct {
        size_t current_row_position;
        size_t current_column_position;
        int scrollable_area_top;
        int scrollable_area_bottom;
        ByteColorCode current_color_code;
        struct CharBuffer *buffer;
} ScreenWriter = {
        0, 0,
        0, BUFFER_HEIGHT,
        (BLACK << 4 | WHITE),
        (struct CharBuffer *) screen_buffer_ptr
};


static void move_buffer_position_left() {
        if (ScreenWriter.current_column_position == 0) {
                ScreenWriter.current_row_position -= 1;
                ScreenWriter.current_column_position = BUFFER_WIDTH - 1;
        }
        else {
                ScreenWriter.current_column_position -= 1;
        }
}

static void move_buffer_position_right() {
        if (ScreenWriter.current_column_position == BUFFER_WIDTH - 1) {
                ScreenWriter.current_row_position += 1;
                ScreenWriter.current_column_position = 0;
        }
        else {
                ScreenWriter.current_column_position += 1;
        }
}

static void save_char_to_buffer(const char c) {
        const struct SingleChar ch = {c, ScreenWriter.current_color_code};

        const auto row = ScreenWriter.current_row_position;
        const auto col = ScreenWriter.current_column_position;

        ScreenWriter.buffer->chars[row][col] = ch;
}

static void scroll_vertical_dir_up() {
        vga_clr_cursor();

        const int scrollable_area_bottom = ScreenWriter.scrollable_area_bottom;

        for (size_t i = ScreenWriter.scrollable_area_top + 1; i < scrollable_area_bottom; ++i) {
                for (size_t j = 0; j < BUFFER_WIDTH; ++j) {
                        ScreenWriter.buffer->chars[i - 1][j] = ScreenWriter.buffer->chars[i][j];
                        vga_put_byte_encoded_color_letter(ScreenWriter.buffer->chars[i - 1][j].ascii_code,
                                                          i - 1, j,
                                                          ScreenWriter.buffer->chars[i - 1][j].color_code
                        );
                }
        }

        const struct SingleChar empty_char = {0x00, ScreenWriter.current_color_code};

        const ByteColorCode prev_color_code = ScreenWriter.buffer->chars[scrollable_area_bottom - 2][BUFFER_WIDTH - 1].
                color_code;

        for (size_t i = 0; i < BUFFER_WIDTH; ++i) {
                ScreenWriter.buffer->chars[scrollable_area_bottom - 1][i] = empty_char;

                vga_put_byte_encoded_color_letter(empty_char.ascii_code,
                                                  scrollable_area_bottom - 1, i,
                                                  prev_color_code
                );
        }
}

static void scroll_horizontal_right(unsigned int row_position, unsigned int column_position) {
        struct SingleChar current_char = {EMPTY_SPACE, ScreenWriter.current_color_code};
        struct SingleChar next_char = ScreenWriter.buffer->chars[row_position][column_position];
        ScreenWriter.buffer->chars[row_position][column_position] = current_char;

        if (column_position == BUFFER_WIDTH - 1) {
                row_position += 1;
                column_position = 0;
        }
        else {
                column_position += 1;
        }

        while (row_position <= BUFFER_HEIGHT - 1 &&
               column_position <= BUFFER_WIDTH - 1) {
                current_char = next_char;
                next_char = ScreenWriter.buffer->chars[row_position][column_position];
                ScreenWriter.buffer->chars[row_position][column_position] = current_char;
                vga_put_byte_encoded_color_letter(current_char.ascii_code, row_position, column_position,
                                                  current_char.color_code);

                if (next_char.ascii_code == 0) {
                        break;
                }

                if (column_position == BUFFER_WIDTH - 1) {
                        row_position += 1;
                        column_position = 0;
                }
                else {
                        column_position += 1;
                }
        }
}

static void scroll_horizontal_left(const unsigned int row_position, const unsigned int column_position) {
        for (size_t i = row_position; i < BUFFER_HEIGHT; ++i) {
                const size_t column_starting_point = (i == row_position) ? column_position : 0;

                for (size_t j = column_starting_point + 1; j <= BUFFER_WIDTH; ++j) {
                        struct SingleChar current_char;

                        if (i == BUFFER_HEIGHT - 1 && j == BUFFER_WIDTH) {
                                const struct SingleChar c = {0x00, (BLACK << 4) | WHITE};
                                current_char = c;
                        }
                        else if (j == BUFFER_WIDTH) {
                                current_char = ScreenWriter.buffer->chars[i + 1][0];
                                if (current_char.ascii_code == ENDL) {
                                        current_char.ascii_code = 0;
                                }
                        }
                        else {
                                current_char = ScreenWriter.buffer->chars[i][j];
                        }

                        ScreenWriter.buffer->chars[i][j - 1] = current_char;

                        vga_put_byte_encoded_color_letter(current_char.ascii_code, i, j - 1, current_char.color_code);
                }
        }
}

static void write_new_line() {
        save_char_to_buffer(ENDL);

        if (ScreenWriter.current_row_position == BUFFER_HEIGHT - 1) {
                scroll_vertical_dir_up();
        }
        else {
                ScreenWriter.current_row_position += 1;
        }

        vga_put_byte_encoded_color_letter(ENDL, ScreenWriter.current_row_position,
                                          ScreenWriter.current_column_position,
                                          ScreenWriter.current_color_code);
        ScreenWriter.current_column_position = 0;
}

static void write_with_line_overflow_if_needed(const char c) {
        save_char_to_buffer(c);

        vga_put_byte_encoded_color_letter(c, ScreenWriter.current_row_position,
                                          ScreenWriter.current_column_position,
                                          ScreenWriter.current_color_code);

        ScreenWriter.current_column_position += 1;
        if (ScreenWriter.current_column_position == BUFFER_WIDTH) {
                write_new_line();
        }
}

typedef struct {
        bool present;
        int val;
} param_t;

static void set_color(const param_t *fg, const param_t *bg) {
        if (!fg->present) {
                return;
        }

        if (fg->val == 0) {
                set_background_color(&ScreenWriter.current_color_code, BLACK, false);
                set_foreground_color(&ScreenWriter.current_color_code, WHITE, false);

                return;
        }

        const bool fg_light = fg->val >= 90 ? true : false;
        if (bg->present) {
                const bool bg_light = bg->val >= 100 ? true : false;

                set_foreground_color(&ScreenWriter.current_color_code, fg->val % 10, fg_light);
                set_background_color(&ScreenWriter.current_color_code, bg->val % 10, bg_light);
        }
        else {
                set_foreground_color(&ScreenWriter.current_color_code, fg->val % 10, fg_light);
        }

        if (fg->val == 39) {
                set_foreground_color(&ScreenWriter.current_color_code, WHITE, false);
        }
        if (bg->present && bg->val == 49) {
                set_background_color(&ScreenWriter.current_color_code, BLACK, false);
        }
}

static void move_cursor(const int row, const int col) {
        vga_clr_cursor();

        ScreenWriter.current_row_position = row;
        ScreenWriter.current_column_position = col;

        vga_update_cursor_position(ScreenWriter.current_row_position, ScreenWriter.current_column_position);
}

static void move_cursor_absolute(param_t *left, param_t *right) {
        if (!left->present) {
                *left = (param_t){true, 1};
        }
        if (!right->present) {
                *right = (param_t){true, 1};
        }

        move_cursor(left->val - 1, right->val - 1);
}

static void define_scrolling_area(param_t *left, param_t *right) {
        if (!left->present || !right->present) {
                return;
        }

        ScreenWriter.scrollable_area_top = left->val - 1;
        ScreenWriter.scrollable_area_bottom = right->val - 1;
}

static void clear_screen(void) {
        memset(ScreenWriter.buffer->chars, 0, BUFFER_HEIGHT * BUFFER_WIDTH * sizeof(struct SingleChar));

        vga_clr_all();
}

static void parse_arguments(const int c, const uint8_t *escape_sequence, param_t *left, param_t *right) {
        const char action[2] = {(char) c, 0};
        const int param_splitter = strcspn(escape_sequence, ";");
        const int action_code = strcspn(escape_sequence, action);

        left->present = false;
        right->present = false;
        char param_left_str[4] = {};
        char param_right_str[4] = {};
        if (escape_sequence[param_splitter + 1] != '\0') {
                memcpy(param_left_str, escape_sequence + 2, param_splitter - 2);
                memcpy(param_right_str, escape_sequence + param_splitter + 1, action_code - (param_splitter + 1));

                *left = (param_t){true, strtoul(param_left_str, nullptr, 10)};
                *right = (param_t){true, strtoul(param_right_str, nullptr, 10)};
        }
        else if (action_code > 2) {
                memcpy(param_left_str, escape_sequence + 2, action_code - 2);

                *left = (param_t){true, strtoul(param_left_str, nullptr, 10)};
        }
}

int handle_escape_sequence(uint8_t *escape_sequence, size_t *escape_sequence_position, const int c) {
        if (*escape_sequence_position == 10) {
                *escape_sequence_position = 0;
                return 1;
        }

        escape_sequence[*escape_sequence_position] = (char) c;
        if (!isalpha(c)) {
                *escape_sequence_position += 1;
                return 0;
        }

        param_t left, right;
        parse_arguments(c, escape_sequence, &left, &right);

        if (c == 'm') {
                set_color(&left, &right);
        }
        else if (c == 'H') {
                move_cursor_absolute(&left, &right);
        }
        else if (c == 'S') {
                scroll_vertical_dir_up();
        }
        else if (c == 'J' && left.present && left.val == 2) {
                clear_screen();
        }
        else if (c == 'G' && left.present) {
                move_cursor(ScreenWriter.current_row_position, left.val - 1);
        }
        else if (c == 'r') {
                define_scrolling_area(&left, &right);
        }

        *escape_sequence_position = 0;
        memset(escape_sequence, 0, 10);

        return 0;
}

void monitor_tm_write_byte(const int c) {
        static uint8_t escape_sequence[10] = {};
        static size_t escape_sequence_position = 0;

        if (escape_sequence_position || c == ESC) {
                handle_escape_sequence(escape_sequence, &escape_sequence_position, c);
                return;
        }

        vga_clr_cursor();

        if (c == ENDL) {
                write_new_line();
        }
        else if (c == BACKSPACE) {
                move_buffer_position_left();

                const auto row = ScreenWriter.current_row_position;
                const auto column = ScreenWriter.current_column_position;
                scroll_horizontal_left(row, column);
        }
        else if (c == CARRIAGE_RETURN) {
                ScreenWriter.current_column_position = 0;
        }
        else if (c == ARROW_LEFT) {
                move_buffer_position_left();
        }
        else if (c == ARROW_RIGHT) {
                move_buffer_position_right();
        }
        else {
                write_with_line_overflow_if_needed(c);
        }

        vga_update_cursor_position(ScreenWriter.current_row_position, ScreenWriter.current_column_position);
}

void monitor_tm_insert_byte(const int c) {
        const auto row = ScreenWriter.current_row_position;
        const auto column = ScreenWriter.current_column_position;

        scroll_horizontal_right(row, column);

        monitor_tm_write_byte(c);
}
