//
// Created by Artur Twardzik on 21/08/2025.
//

#include "tty.h"

#include "config.h"
#include "errno.h"
#include "escape_codes.h"
#include "libc.h"
#include "signal.h"
#include "drivers/keyboard.h"
#include "drivers/uart.h"
#include "drivers/vga.h"
#include "kernel/memory.h"
#include "kernel/resources.h"


//TODO: probably UART should be separated from screen terminal!


extern uint8_t __screen_buffer_start__[];
extern uint8_t __screen_buffer_length__[];

uint8_t *const screen_buffer_ptr = __screen_buffer_start__;
const uint8_t *const screen_length_ptr = __screen_buffer_length__;


void raw_put_letter(
        const char letter, const unsigned int row_letter_position,
        const unsigned int column_letter_position,
        const ByteColorCode color_code
) {
        if (kconf->io_dev.uart.enabled) {
                if (letter != ENDL) {
                        uart_set_cursor(row_letter_position, column_letter_position);
                        uart_change_color(color_code);
                }

                if (isprint(letter)) {
                        uart_putc(letter);
                }
                else {
                        uart_putc(EMPTY_SPACE);
                }
        }

        if (kconf->io_dev.vga_display.enabled) {
                vga_put_byte_encoded_color_letter(letter, row_letter_position, column_letter_position, color_code);
        }
}

struct SingleChar {
        uint8_t ascii_code;
        ByteColorCode color_code;
};

struct CharBuffer {
        struct SingleChar chars[BUFFER_HEIGHT][BUFFER_WIDTH];
};

//TODO: tty should be dynamic kernel "process", so that we can open multiple files
static struct {
        size_t current_row_position;
        size_t current_column_position;
        ByteColorCode current_color_code;
        struct CharBuffer *buffer;
} ScreenWriter = {0, 0, (BLACK << 4 | WHITE), (struct CharBuffer *) screen_buffer_ptr};

void init_tty() {
        if (kconf->io_dev.uart.enabled) {
                uart_init();
                uart_clr_screen();
        }

        if (kconf->io_dev.ps2_keyboard.enabled) {
                init_keyboard(27, 26);
        }

        if (kconf->io_dev.vga_display.enabled) {
                vga_init(9, 10, 3);

                vga_setup_cursor(0, 0, ScreenWriter.current_color_code, 500'000);
        }
}

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

static void scroll_vertical() {
        if (!kconf->io_dev.uart.enabled) {
                uart_puts("\x1b[S");
        }
        if (!kconf->io_dev.vga_display.enabled) {
                return;
        }

        vga_clr_all();

        for (size_t i = 1; i < BUFFER_HEIGHT; ++i) {
                for (size_t j = 0; j < BUFFER_WIDTH; ++j) {
                        ScreenWriter.buffer->chars[i - 1][j] = ScreenWriter.buffer->chars[i][j];
                        vga_put_byte_encoded_color_letter(ScreenWriter.buffer->chars[i - 1][j].ascii_code, i - 1, j,
                                                          ScreenWriter.buffer->chars[i - 1][j].color_code
                        );
                }
        }

        const struct SingleChar empty_char = {0x00, ScreenWriter.current_color_code};

        for (size_t i = 0; i < BUFFER_WIDTH; ++i) {
                ScreenWriter.buffer->chars[BUFFER_HEIGHT - 1][i] = empty_char;
                vga_put_byte_encoded_color_letter(empty_char.ascii_code,
                                                  BUFFER_HEIGHT - 1,
                                                  i,
                                                  ScreenWriter.buffer->chars[BUFFER_HEIGHT - 2][BUFFER_WIDTH - 1].
                                                  color_code
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
                raw_put_letter(current_char.ascii_code, row_position, column_position, current_char.color_code);

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

                        raw_put_letter(current_char.ascii_code, i, j - 1, current_char.color_code);
                        uart_set_cursor(i, j - 1);

                        if (current_char.ascii_code == 0) {
                                if (kconf->io_dev.uart.enabled) {
                                        uart_set_cursor(row_position, column_position);
                                }
                                return;
                        }
                }
        }
}

static void write_new_line() {
        save_char_to_buffer(ENDL);

        if (ScreenWriter.current_row_position == BUFFER_HEIGHT - 1) {
                scroll_vertical();
        }
        else {
                ScreenWriter.current_row_position += 1;
        }

        raw_put_letter(ENDL, ScreenWriter.current_row_position,
                       ScreenWriter.current_column_position,
                       ScreenWriter.current_color_code);
        ScreenWriter.current_column_position = 0;
}

static void write_with_line_overflow_if_needed(const char c) {
        save_char_to_buffer(c);

        raw_put_letter(c, ScreenWriter.current_row_position,
                       ScreenWriter.current_column_position,
                       ScreenWriter.current_color_code);

        ScreenWriter.current_column_position += 1;
        if (ScreenWriter.current_column_position == BUFFER_WIDTH) {
                write_new_line();
        }
}


void write_byte(const int c) {
        static uint8_t escape_sequence[10] = {};
        static size_t escape_sequence_position = 0;

        if (escape_sequence_position || c == ESC) {
                if (c == 'm') {
                        if (escape_sequence[2] == '0' && escape_sequence_position == 3) {
                                set_background_color(&ScreenWriter.current_color_code, BLACK);
                                set_foreground_color(&ScreenWriter.current_color_code, WHITE);
                                escape_sequence_position = 0;

                                return;
                        }

                        ScreenWriter.current_color_code = decode_escape_colors(escape_sequence);
                        escape_sequence_position = 0;

                        return;
                }

                escape_sequence[escape_sequence_position] = (char) c;
                escape_sequence_position += 1;

                return;
        }

        if (kconf->io_dev.vga_display.enabled) {
                vga_clr_cursor();
        }

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

                if (kconf->io_dev.uart.enabled) {
                        uart_putc(c);
                }
        }
        else if (c == ARROW_RIGHT) {
                move_buffer_position_right();

                if (kconf->io_dev.uart.enabled) {
                        uart_putc(c);
                }
        }
        else {
                write_with_line_overflow_if_needed(c);
        }

        if (kconf->io_dev.vga_display.enabled) {
                vga_update_cursor_position(ScreenWriter.current_row_position, ScreenWriter.current_column_position);
        }
}

static void insert_byte(const int c) {
        const auto row = ScreenWriter.current_row_position;
        const auto column = ScreenWriter.current_column_position;

        scroll_horizontal_right(row, column);

        write_byte(c);
}

static void write_string(const char *str) {
        const char *c = str;

        while (*c != EOL) {
                write_byte(*c);
                c += 1;
        }
}

//TODO: remove and integrate with struct CharBuffer
static struct {
        wait_queue_head_t read_wait;

        size_t length;
        char buffer[] __attribute__((counted_by(length)));
} *keyboard_device_file_stream;

static int keyboard_buffer_final_length = 0;
static int keyboard_buffer_current_position = 0;

static bool tty_echo_on = true;
static bool tty_canonical_mode = true;
static bool character_present = false;
static bool newline_present = false;

void *get_current_keyboard_buffer_offset() {
        return keyboard_device_file_stream->buffer + keyboard_buffer_final_length;
}

static void insert_and_shift(const char c, const int pos_insert, const int len) {
        int temp = pos_insert;
        char next_char = *(keyboard_device_file_stream->buffer + temp);
        *(keyboard_device_file_stream->buffer + temp) = c;

        temp += 1;
        while (temp < len) {
                const char current_char = next_char;
                next_char = *(keyboard_device_file_stream->buffer + temp);
                *(keyboard_device_file_stream->buffer + temp) = current_char;

                temp += 1;
        }
}

static void delete_and_shift(const int pos_delete, const int len) {
        int temp = pos_delete;

        while (temp < len) {
                *(keyboard_device_file_stream->buffer + temp) = *(keyboard_device_file_stream->buffer + temp + 1);

                temp += 1;
        }
}

static void tty_echo(int c) {
        if (!tty_echo_on) {
                return;
        }

        write_byte(c);
}

void handle_special_character(int c) {
        if (c == ETX) {
                tty_echo('^');
                tty_echo('C');

                struct Process *process = nullptr;
                if (keyboard_device_file_stream->read_wait) {
                        process = pop_from_wait_queue(&keyboard_device_file_stream->read_wait);
                }
                else {
                        process = scheduler_get_current_process();
                }
                if (process) {
                        signal_notify(process, SIGINT);
                }
        }
        else if (c == BACKSPACE) {
                if (keyboard_buffer_current_position) {
                        keyboard_buffer_final_length -= 1;
                        keyboard_buffer_current_position -= 1;
                        delete_and_shift(keyboard_buffer_current_position, keyboard_buffer_final_length);

                        tty_echo(c);
                }
        }
        else if (c == ARROW_LEFT) {
                if (keyboard_buffer_current_position) {
                        keyboard_buffer_current_position -= 1;
                        tty_echo(c);
                }
        }
        else if (c == ARROW_RIGHT) {
                if (keyboard_buffer_current_position < keyboard_buffer_final_length) {
                        keyboard_buffer_current_position += 1;
                        tty_echo(c);
                }
        }
}

void write_to_keyboard_buffer(int c) {
        static char escape_sequence[4] = {};
        static size_t escape_sequence_position = 0;

        if (escape_sequence_position || c == ESC) {
                escape_sequence[escape_sequence_position] = (char) c;

                if (escape_sequence_position == 2) {
                        c = (escape_sequence[0] << 16) | (escape_sequence[1] << 8) | escape_sequence[2];

                        escape_sequence_position = 0;
                }
                else {
                        escape_sequence_position += 1;
                        return;
                }
        }

        if (c == ETX || c == BACKSPACE || c == ARROW_LEFT || c == ARROW_RIGHT) {
                handle_special_character(c);
                goto wake_up_if_applicable;
        }

        // TODO: it would be wise to resize buffer if the contents do not fit

        keyboard_buffer_final_length += 1;
        keyboard_buffer_current_position += 1;
        if (c == ENDL) {
                *(keyboard_device_file_stream->buffer + keyboard_buffer_final_length - 1) = ENDL;

                while (keyboard_buffer_current_position < keyboard_buffer_final_length) {
                        keyboard_buffer_current_position += 1;
                        tty_echo(ARROW_RIGHT);
                }
                tty_echo(ENDL);

                newline_present = true;
                goto wake_up_if_applicable;
        }

        if (keyboard_buffer_current_position < keyboard_buffer_final_length) {
                insert_and_shift(c, keyboard_buffer_current_position - 1, keyboard_buffer_final_length);

                if (tty_echo_on) {
                        insert_byte(c);
                }

                goto wake_up_if_applicable;
        }

        *(keyboard_device_file_stream->buffer + keyboard_buffer_current_position - 1) = (char) c;
        tty_echo(c);


wake_up_if_applicable:
        character_present = true;
        if (!tty_canonical_mode || (tty_canonical_mode && newline_present)) {
                wake_up_interruptible(&keyboard_device_file_stream->read_wait);
        }
}

int get_written_characters_count(void) {
        if (newline_present) {
                const auto temp = keyboard_buffer_final_length;

                return temp;
        }

        return character_present ? 1 : 0;
}

static void reset_keyboard_buffer(void) {
        newline_present = false;
        character_present = false;
        keyboard_buffer_final_length = 0;
        keyboard_buffer_current_position = 0;
}

static bool tty_is_ready() {
        if (tty_canonical_mode) {
                return newline_present;
        }

        return character_present;
}

static ssize_t tty_read(struct File *, void *buf, const size_t count, off_t file_offset) {
        wait_event_interruptible(&keyboard_device_file_stream->read_wait, tty_is_ready);

        const int stream_size = get_written_characters_count();
        const char *stream = keyboard_device_file_stream->buffer;
        if (stream_size == 0) {
                // errno = EINTR;
                return -1;
        }

        int offset = 0;
        char *ptr = (char *) buf;
        while (offset < count && offset < stream_size) {
                ptr[offset] = stream[offset];

                offset += 1;
        }

        reset_keyboard_buffer();
        return offset;
}

static ssize_t tty_write(struct File *, void *buf, const size_t count, off_t file_offset) {
        const char *ptr = buf;

        for (int i = 0; i < count; i++) {
                write_byte(*ptr++);
        }

        return count;
}

int tty_ioctl(struct File *file, const uint64_t request, void *arg) {
        switch (request) {
                case TTY_ECHO:
                        tty_echo_on = *(bool *) arg;
                        break;
                case TTY_CANONICAL:
                        tty_canonical_mode = *(bool *) arg;
                        break;
                case TTY_CLEAR_SCREEN:
                        break;
                default:
                        return -1;
        }

        return 0;
}

int setup_tty_chrfile(struct VFS_Inode *mount_point) {
        if (!mount_point) {
                return -ENOENT;
        }

        struct FileOperations *stdio_op = kmalloc(sizeof(*stdio_op));
        if (!stdio_op) {
                return -ENOMEM;
        }
        memset(stdio_op, 0, sizeof(*stdio_op));

        stdio_op->read = tty_read;
        stdio_op->write = tty_write;
        stdio_op->ioctl = tty_ioctl;
        kfree(mount_point->i_fop);
        mount_point->i_fop = stdio_op;

        constexpr size_t buf_size = 512;

        keyboard_device_file_stream = kmalloc(
                sizeof(*keyboard_device_file_stream)
                + (buf_size - 1) * sizeof(char)
        );

        // struct RAMFS_Inode *inode = (struct RAMFS_Inode *) mount_point;
        // if (inode->file_begin == nullptr) {
        //         //todo: write per-tty configuration replacing static variables from this file
        // }

        keyboard_device_file_stream->length = buf_size;
        keyboard_device_file_stream->read_wait = nullptr;

        return 0;
}


int printk(const char *buf) {
        const char *ptr = buf;
        const size_t count = strlen(ptr);

        for (int i = 0; i < count; i++) {
                write_byte(*ptr++);
        }

        return count;
}

static constexpr int STATUS_BAR_SIZE = 11;
static int current_status_len;
static int current_status_step;
static constexpr int MAX_STATUS_STEP = 8;

enum StartupStatus {
        STATUS_NONE,
        STATUS_OK,
        STATUS_FAILED,
};

static void printk_status(enum StartupStatus status) {
        switch (status) {
                case STATUS_NONE:
                        printk("\r[        ] ");
                        break;
                case STATUS_OK:
                        printk("\r[   \x1b[92;40mOK\x1b[0m   ] ");
                        break;
                case STATUS_FAILED:
                        printk("\r[ \x1b[91;40mFAILED\x1b[0m ] ");
                        break;
        }
}

void printk_status_init(const char *msg) {
        current_status_len = strlen(msg);
        current_status_step = 0;

        printk_status(STATUS_NONE);
        printk(msg);
}

void printk_status_step(void) {
        printk("\r[");
        for (int i = 0; i < current_status_step; ++i) {
                printk("#");
        }

        if (current_status_step < MAX_STATUS_STEP) {
                current_status_step += 1;
                printk("#");
        }

        for (int i = current_status_step; i < MAX_STATUS_STEP; ++i) {
                printk(" ");
        }

        printk("] ");
}

void printk_status_finish(const int return_code) {
        const enum StartupStatus status = (return_code == 0) ? STATUS_OK : STATUS_FAILED;
        printk_status(status);

        for (int i = 0; i < current_status_len + 1; ++i) {
                write_byte(ARROW_RIGHT);
        }

        printk("\n");
}

void printk_status_info(const char *msg) {
        for (int i = 0; i < STATUS_BAR_SIZE; ++i) {
                printk(" ");
        }

        printk(msg);
        printk("\n");
}
