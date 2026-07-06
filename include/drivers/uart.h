//
// Created by Artur Twardzik on 30/12/2024.
//

#ifndef UART_H
#define UART_H

#include "escape_codes.h"
#include "libc.h"
#include "types.h"


void uart_init(void);

void uart_putc(int c);

void uart_puts(const char *str);

const int uart_getc(void);

void uart_clr_screen(void);

static inline void uart_set_cursor(const unsigned int row_letter_position, const unsigned int column_letter_position) {
        static size_t current_uart_x_position = 0;
        static size_t current_uart_y_position = 0;

        if (current_uart_x_position == column_letter_position && current_uart_y_position == row_letter_position) {
                return;
        }

        char row_position[5] = {};
        char col_position[5] = {};

        uart_putc(0x1b);
        uart_putc('[');

        itoa(row_letter_position + 1, row_position, 10);
        uart_puts(row_position);

        uart_putc(';');

        itoa(column_letter_position + 1, col_position, 10);
        uart_puts(col_position);

        uart_putc('H');

        current_uart_x_position = column_letter_position + 1;
        current_uart_y_position = row_letter_position + 1;
}

static inline void uart_change_color(ByteColorCode color_code) {
        static ByteColorCode current_uart_color = (BLACK << 4) | WHITE;
        if (current_uart_color == color_code) {
                return;
        }

        current_uart_color = color_code;

        const uint8_t foreground_color_encoded = color_code & FOREGROUND_COLOR_BITS;
        const bool foreground_color_light = color_code & FOREGROUND_LIGHT_COLOR_BIT;
        uint8_t background_color_encoded = (color_code & BACKGROUND_COLOR_BITS) >> 4;
        const bool background_color_light = color_code & BACKGROUND_LIGHT_COLOR_BIT;

        if (background_color_encoded == BLACK) {
                background_color_encoded = 9;
        }

        uart_putc(0x1b);
        uart_putc('[');
        if (foreground_color_light) {
                uart_putc('9');
        }
        else {
                uart_putc('3');
        }
        uart_putc(foreground_color_encoded + 0x30);

        uart_putc(';');

        if (background_color_light) {
                uart_putc('1');
                uart_putc('0');
        }
        else {
                uart_putc('4');
        }
        uart_putc(background_color_encoded + 0x30);

        uart_putc('m');
}

#endif //UART_H
