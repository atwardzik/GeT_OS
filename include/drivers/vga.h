//
// Created by Artur Twardzik on 24/07/2025.
//

#ifndef KERNEL_VGA_H
#define KERNEL_VGA_H

#include "escape_codes.h"
#include "types.h"

#include <stdint.h>

constexpr int SCREEN_WIDTH = 640;
constexpr int SCREEN_HEIGHT = 480;

constexpr int BUFFER_WIDTH = 80;
constexpr int BUFFER_HEIGHT = 40;

constexpr int TOP_IMAGE_PADDING = 100 * SCREEN_WIDTH;

typedef uint8_t PhysicalColor;

struct PixelMap {
        uint8_t *map;
        size_t width;
        size_t height;
};

void vga_init(uint32_t hsync_pin, uint32_t vsync_pin, uint32_t pin_red0);

void vga_put_physical_color_letter(
        char letter, unsigned int row_letter_position, unsigned int column_letter_position,
        PhysicalColor foreground_color,
        PhysicalColor background_color
);

void vga_put_byte_encoded_color_letter(
        char letter, unsigned int row_letter_position,
        unsigned int column_letter_position, ByteColorCode color_code
);

void vga_put_pixel_map(
        const struct PixelMap *pixel_map, unsigned int row_pixel_position,
        unsigned int column_pixel_position
);


void vga_setup_cursor(unsigned int row, unsigned int column, ByteColorCode color_code, uint32_t us);

void vga_update_cursor_position(unsigned int row, unsigned int column);

void vga_update_cursor_color(ByteColorCode color_code);

enum VGACursorShape {
        BLOCK     = 1,
        UNDERLINE = 4,
        BAR       = 6,
};

void vga_change_cursor_shape(enum VGACursorShape shape);

extern void vga_set_cursor_off(void);

void vga_xor_cursor(void);

void vga_clr_cursor(void);

void vga_clr_screen(void);

void vga_clr_all(void);

#endif // KERNEL_VGA_H
