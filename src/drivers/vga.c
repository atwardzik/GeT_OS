//
// Created by Artur Twardzik on 21/08/2025.
//

#include "drivers/vga.h"

#include "escape_codes.h"
#include "font.h"

#include <stdint.h>

extern const uint8_t __vidram_start__[];
const uint8_t *const vidram_start_ptr = __vidram_start__;

extern void hsync_gen_init(uint32_t pin);

extern void vsync_gen_init(uint32_t pin);

extern void rgb_gen_init(uint32_t pin_red0);

extern void setup_vga_dma(void);

extern void vga_start(void);

extern void vga_set_cursor_blink(uint32_t us);


enum PhysicalColorCode {
        PHYSICAL_BLACK         = 0b000000, // #000000
        PHYSICAL_RED           = 0b000010, // #aa0000
        PHYSICAL_GREEN         = 0b001001, // #55aa00
        PHYSICAL_YELLOW        = 0b001010, // #aaaa00
        PHYSICAL_BLUE          = 0b100000, // #0000aa
        PHYSICAL_MAGENTA       = 0b100010, // #aa00aa
        PHYSICAL_CYAN          = 0b101001, // #55aaaa
        PHYSICAL_WHITE         = 0b111111, // #ffffff
        PHYSICAL_DARK_GRAY     = 0b010101, // #555555
        PHYSICAL_RED_LIGHT     = 0b000011, // #ff0000
        PHYSICAL_GREEN_LIGHT   = 0b001100, // #00ff00
        PHYSICAL_YELLOW_LIGHT  = 0b001111, // #ffff00
        PHYSICAL_BLUE_LIGHT    = 0b110000, // #0000ff
        PHYSICAL_MAGENTA_LIGHT = 0b110011, // #ff00ff
        PHYSICAL_CYAN_LIGHT    = 0b111101, // #55ffff
        PHYSICAL_LIGHT_GRAY    = 0b101010, // #aaaaaa
};

void vga_init(const uint32_t hsync_pin, const uint32_t vsync_pin, const uint32_t pin_red0) {
        vga_clr_screen();

        hsync_gen_init(hsync_pin);
        vsync_gen_init(vsync_pin);
        rgb_gen_init(pin_red0);

        setup_vga_dma();

        vga_start();
}

static void set_colors_from_escape(
        PhysicalColor *foreground_color, PhysicalColor *background_color,
        const ByteColorCode escape_color_code
) {
        const uint8_t foreground_color_encoded = escape_color_code & FOREGROUND_COLOR_ENCODING_BITS;
        const bool foreground_color_light = escape_color_code & FOREGROUND_LIGHT_COLOR_BIT;
        const uint8_t background_color_encoded = (escape_color_code & BACKGROUND_COLOR_ENCODING_BITS) >> 4;
        const bool background_color_light = escape_color_code & BACKGROUND_LIGHT_COLOR_BIT;

        if (foreground_color_light) {
                switch (foreground_color_encoded) {
                        case BLACK:
                                *foreground_color = PHYSICAL_DARK_GRAY;
                                break;
                        case RED:
                                *foreground_color = PHYSICAL_RED_LIGHT;
                                break;
                        case GREEN:
                                *foreground_color = PHYSICAL_GREEN_LIGHT;
                                break;
                        case YELLOW:
                                *foreground_color = PHYSICAL_YELLOW_LIGHT;
                                break;
                        case BLUE:
                                *foreground_color = PHYSICAL_BLUE_LIGHT;
                                break;
                        case MAGENTA:
                                *foreground_color = PHYSICAL_MAGENTA_LIGHT;
                                break;
                        case CYAN:
                                *foreground_color = PHYSICAL_CYAN_LIGHT;
                                break;
                        case WHITE:
                                *foreground_color = PHYSICAL_LIGHT_GRAY;
                                break;
                        default:
                                *foreground_color = PHYSICAL_WHITE;
                                break;
                }
        }
        else {
                switch (foreground_color_encoded) {
                        case BLACK:
                                *foreground_color = PHYSICAL_BLACK;
                                break;
                        case RED:
                                *foreground_color = PHYSICAL_RED;
                                break;
                        case GREEN:
                                *foreground_color = PHYSICAL_GREEN;
                                break;
                        case YELLOW:
                                *foreground_color = PHYSICAL_YELLOW;
                                break;
                        case BLUE:
                                *foreground_color = PHYSICAL_BLUE;
                                break;
                        case MAGENTA:
                                *foreground_color = PHYSICAL_MAGENTA;
                                break;
                        case CYAN:
                                *foreground_color = PHYSICAL_CYAN;
                                break;
                        case WHITE:
                                *foreground_color = PHYSICAL_WHITE;
                                break;
                        default:
                                *foreground_color = PHYSICAL_WHITE;
                                break;
                }
        }

        if (background_color_light) {
                switch (background_color_encoded) {
                        case BLACK:
                                *background_color = PHYSICAL_DARK_GRAY;
                                break;
                        case RED:
                                *background_color = PHYSICAL_RED_LIGHT;
                                break;
                        case GREEN:
                                *background_color = PHYSICAL_GREEN_LIGHT;
                                break;
                        case YELLOW:
                                *background_color = PHYSICAL_YELLOW_LIGHT;
                                break;
                        case BLUE:
                                *background_color = PHYSICAL_BLUE_LIGHT;
                                break;
                        case MAGENTA:
                                *background_color = PHYSICAL_MAGENTA_LIGHT;
                                break;
                        case CYAN:
                                *background_color = PHYSICAL_CYAN_LIGHT;
                                break;
                        case WHITE:
                                *background_color = PHYSICAL_LIGHT_GRAY;
                                break;
                        default:
                                *background_color = PHYSICAL_BLACK;
                                break;
                }
        }
        else {
                switch (background_color_encoded) {
                        case BLACK:
                                *background_color = PHYSICAL_BLACK;
                                break;
                        case RED:
                                *background_color = PHYSICAL_RED;
                                break;
                        case GREEN:
                                *background_color = PHYSICAL_GREEN;
                                break;
                        case YELLOW:
                                *background_color = PHYSICAL_YELLOW;
                                break;
                        case BLUE:
                                *background_color = PHYSICAL_BLUE;
                                break;
                        case MAGENTA:
                                *background_color = PHYSICAL_MAGENTA;
                                break;
                        case CYAN:
                                *background_color = PHYSICAL_CYAN;
                                break;
                        case WHITE:
                                *background_color = PHYSICAL_WHITE;
                                break;
                        default:
                                *background_color = PHYSICAL_BLACK;
                                break;
                }
        }
}

void vga_put_physical_color_letter(
        const char letter, unsigned int row_letter_position,
        unsigned int column_letter_position,
        const PhysicalColor foreground_color,
        const PhysicalColor background_color
) {
        const uint8_t *letter_lookup = font8x8[letter];

        const unsigned int row_padding = row_letter_position * SCREEN_WIDTH;
        row_letter_position = row_letter_position * SCREEN_WIDTH * FONT_HEIGHT;
        column_letter_position = column_letter_position * FONT_WIDTH;

        const unsigned int position = row_letter_position + row_padding + column_letter_position + TOP_IMAGE_PADDING;

        for (size_t i = 0; i < 8; ++i) {
                const uint8_t pixel_line = letter_lookup[i];
                for (size_t j = 0; j < 8; ++j) {
                        // As you can see, the first bit shifted is the least significant one.
                        // This bit goes to the very first bit position in bitmap. Therefore,
                        // the array containing glyphs has those glyphs mirrored.
                        if (pixel_line & (1 << j)) {
                                *(uint8_t *) (vidram_start_ptr + i * SCREEN_WIDTH + j + position) =
                                        foreground_color;
                        }
                        else {
                                *(uint8_t *) (vidram_start_ptr + i * SCREEN_WIDTH + j + position) =
                                        background_color;
                        }
                }
        }
}

void vga_put_byte_encoded_color_letter(
        const char letter, unsigned int row_letter_position,
        unsigned int column_letter_position,
        const ByteColorCode color_code
) {
        PhysicalColor foreground_color;
        PhysicalColor background_color;
        set_colors_from_escape(&foreground_color, &background_color, color_code);

        vga_put_physical_color_letter(letter, row_letter_position, column_letter_position,
                                      foreground_color,
                                      background_color);
}

void vga_put_pixel_map(
        const struct PixelMap *pixel_map, const unsigned int row_pixel_position,
        const unsigned int column_pixel_position
) {
        const unsigned int position = row_pixel_position * SCREEN_WIDTH + column_pixel_position + TOP_IMAGE_PADDING;

        for (size_t i = 0; i < pixel_map->height; ++i) {
                for (size_t j = 0; j < pixel_map->width; ++j) {
                        *(uint8_t *) (vidram_start_ptr + i * SCREEN_WIDTH + j + position) =
                                *(pixel_map->map + i * pixel_map->width + j);
                }
        }
}

static bool cursor_on = false;
static size_t cursor_row_position = 0;
static size_t cursor_column_position = 0;
static PhysicalColor current_cursor_foreground_color = PHYSICAL_WHITE;
static PhysicalColor current_cursor_background_color = PHYSICAL_BLACK;

static uint8_t current_letter_under_cursor[8][8] = {};
static struct PixelMap current_pixels_under_cursor_map = {(uint8_t *) current_letter_under_cursor, 8, 8};

static uint8_t current_cursor[8][8] = {};
static struct PixelMap current_cursor_map = {(uint8_t *) current_cursor, 8, 8};

static void vga_determine_letter_under_cursor() {
        unsigned int row_letter_position = cursor_row_position;
        unsigned int column_letter_position = cursor_column_position;

        const unsigned int row_padding = row_letter_position * SCREEN_WIDTH;
        row_letter_position = row_letter_position * SCREEN_WIDTH * FONT_HEIGHT;
        column_letter_position = column_letter_position * FONT_WIDTH;

        const unsigned int position = row_letter_position + row_padding + column_letter_position + TOP_IMAGE_PADDING;

        for (size_t i = 0; i < 8; ++i) {
                for (size_t j = 0; j < 8; ++j) {
                        const uint8_t current_pixel_code = *(uint8_t *) (vidram_start_ptr
                                                                         + i * SCREEN_WIDTH + j + position);

                        current_letter_under_cursor[i][j] = current_pixel_code;

                        if (current_pixel_code) {
                                current_cursor[i][j] = PHYSICAL_BLACK;
                        }
                        else {
                                current_cursor[i][j] = PHYSICAL_WHITE;
                        }
                }
        }
}

void vga_update_cursor_position(const unsigned int row, const unsigned int column) {
        if (cursor_on) {
                vga_determine_letter_under_cursor();
                vga_clr_cursor();
        }

        cursor_row_position = row;
        cursor_column_position = column;

        vga_determine_letter_under_cursor();
}

void vga_setup_cursor(
        const unsigned int row, const unsigned int column, const ByteColorCode color_code,
        const uint32_t us
) {
        PhysicalColor foreground_color;
        PhysicalColor background_color;
        set_colors_from_escape(&foreground_color, &background_color, color_code);

        current_cursor_foreground_color = foreground_color;
        current_cursor_background_color = background_color;

        cursor_row_position = row;
        cursor_column_position = column;
        vga_determine_letter_under_cursor();

        vga_set_cursor_blink(us);
        vga_xor_cursor(); // start with cursor on
}

void vga_update_cursor_color(const ByteColorCode color_code) {
        PhysicalColor foreground_color;
        PhysicalColor background_color;
        set_colors_from_escape(&foreground_color, &background_color, color_code);

        current_cursor_foreground_color = foreground_color;
        current_cursor_background_color = background_color;
}

void vga_xor_cursor() {
        const auto row_position = cursor_row_position * FONT_HEIGHT + cursor_row_position;
        const auto col_position = cursor_column_position * FONT_WIDTH;
        if (cursor_on) {
                vga_put_pixel_map(&current_cursor_map, row_position, col_position);
                cursor_on = false;
        }
        else {
                vga_put_pixel_map(&current_pixels_under_cursor_map, row_position, col_position);
                cursor_on = true;
        }
}

void vga_clr_cursor() {
        const auto row_position = cursor_row_position * FONT_HEIGHT + cursor_row_position;
        const auto col_position = cursor_column_position * FONT_WIDTH;

        vga_put_pixel_map(&current_pixels_under_cursor_map, row_position, col_position);
        cursor_on = false;
}

void vga_clr_all() {
        vga_clr_cursor();
        // vga_set_cursor_off();
        vga_clr_screen();
}
