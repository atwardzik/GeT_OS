//
// Created by Artur Twardzik on 21/08/2025.
//

#include "tty.h"

#include "drivers/keyboard.h"
#include "drivers/monitor_text_mode.h"
#include "drivers/uart.h"
#include "drivers/vga.h"

#include "kernel/memory.h"
#include "kernel/resources.h"

#include "ascii_char_codes.h"
#include "config.h"
#include "errno.h"
#include "signal.h"


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

                vga_setup_cursor(0, 0, (BLACK << 4 | WHITE), 500'000);
        }
}

static void write_byte(int c) {
        if (kconf->io_dev.vga_display.enabled) {
                monitor_tm_write_byte(c);
        }

        if (kconf->io_dev.uart.enabled) {
                if (c == BACKSPACE) {
                        uart_putc(ARROW_LEFT);
                        uart_putc(UART_DCH);
                }
                else {
                        uart_putc(c);
                }
        }
}

static void insert_byte(int c) {
        if (kconf->io_dev.vga_display.enabled) {
                monitor_tm_insert_byte(c);
        }

        if (kconf->io_dev.uart.enabled) {
                uart_putc(UART_ICH);
                uart_putc(c);
        }
}

static struct {
        wait_queue_head_t read_wait;

        pid_t fg_pgid;

        size_t length;
        char buffer[] __attribute__((counted_by(length)));
} *keyboard_device_file_stream;

static int keyboard_buffer_final_length = 0;
static int keyboard_buffer_current_position = 0;

static bool tty_echo_on = true;
static bool tty_canonical_mode = true;
static bool character_present = false;
static bool newline_present = false;

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

static void handle_special_character(int c) {
        if (c == ETX) {
                tty_echo('^');
                tty_echo('C');

                struct Process *process = nullptr;
                const struct Process *wq_top = top_from_wait_queue(&keyboard_device_file_stream->read_wait);
                if (wq_top && wq_top->pgid == keyboard_device_file_stream->fg_pgid) {
                        process = pop_from_wait_queue(&keyboard_device_file_stream->read_wait);
                }
                else {
                        process = scheduler_get_current_process();
                }

                if (process && process->pgid == keyboard_device_file_stream->fg_pgid) {
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
        else if (c == ARROW_UP || c == ARROW_DOWN) {
                if (!tty_canonical_mode) {
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

        if (c == ETX || c == BACKSPACE || c == ARROW_LEFT || c == ARROW_RIGHT || c == ARROW_UP || c == ARROW_DOWN) {
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

static int get_written_characters_count(void) {
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
        const struct Process *process = scheduler_get_current_process();
        if (process->pgid != keyboard_device_file_stream->fg_pgid) {
                return -1; //not in fg process group
        }

        wait_event_interruptible(&keyboard_device_file_stream->read_wait, tty_is_ready);

        const int stream_size = get_written_characters_count();
        const char *stream = keyboard_device_file_stream->buffer;
        if (stream_size == 0) {
                return -1;
        }

        int offset = 0;
        char *ptr = (char *) buf;
        while (offset < count && offset < stream_size) {
                ptr[offset] = stream[offset];

                offset += 1;
        }

        reset_keyboard_buffer();
        ptr[offset] = 0;
        return offset;
}

static ssize_t tty_write(struct File *, void *buf, const size_t count, off_t file_offset) {
        const char *ptr = buf;

        for (int i = 0; i < count; i++) {
                write_byte(*ptr++);
        }

        return count;
}

static int tty_ioctl(struct File *file, const uint64_t request, void *arg) {
        switch (request) {
                case TTY_ECHO:
                        tty_echo_on = *(bool *) arg;
                        break;
                case TTY_CANONICAL:
                        tty_canonical_mode = *(bool *) arg;
                        break;
                case TTY_CLEAR_SCREEN:
                        break;
                case TTY_GPGRP:
                        *(pid_t *) arg = keyboard_device_file_stream->fg_pgid;
                        break;
                case TTY_SPGRP:
                        keyboard_device_file_stream->fg_pgid = *(pid_t *) arg;
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
        //         //TODO: write per-tty configuration replacing static variables from this file
        // }

        keyboard_device_file_stream->fg_pgid = 0;
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
