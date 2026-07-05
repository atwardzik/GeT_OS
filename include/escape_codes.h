//
// Created by Artur Twardzik on 25/08/2025.
//

#ifndef OS_ESCAPE_CODES_H
#define OS_ESCAPE_CODES_H

#include <stdint.h>

enum EscapeColor {
        BLACK         = 0b0000,
        RED           = 0b0001,
        GREEN         = 0b0010,
        YELLOW        = 0b0011,
        BLUE          = 0b0100,
        MAGENTA       = 0b0101,
        CYAN          = 0b0110,
        WHITE         = 0b0111,
        DARK_GRAY     = 0b1000,
        LIGHT_RED     = 0b1001,
        LIGHT_GREEN   = 0b1010,
        LIGHT_YELLOW  = 0b1011,
        LIGHT_BLUE    = 0b1100,
        LIGHT_MAGENTA = 0b1101,
        LIGHT_CYAN    = 0b1110,
        LIGHT_GRAY    = 0b1111,
};

typedef uint8_t ByteColorCode;

constexpr uint8_t FOREGROUND_LIGHT_COLOR_BIT = 1 << 3;
constexpr uint8_t BACKGROUND_LIGHT_COLOR_BIT = 1 << 7;
constexpr uint8_t BACKGROUND_COLOR_ENCODING_BITS = 0x70;
constexpr uint8_t FOREGROUND_COLOR_ENCODING_BITS = 0x07;
constexpr uint8_t BACKGROUND_COLOR_BITS = 0xf0;
constexpr uint8_t FOREGROUND_COLOR_BITS = 0x0f;

static inline void set_foreground_color(ByteColorCode *color_code, const enum EscapeColor color) {
        *color_code &= BACKGROUND_COLOR_BITS;
        *color_code |= color;
}

static inline void set_background_color(ByteColorCode *color_code, const enum EscapeColor color) {
        *color_code &= FOREGROUND_COLOR_BITS;
        *color_code |= color << 4;
}

static inline void set_byte_color(ByteColorCode *color_code,
                           const enum EscapeColor foreground_color,
                           const bool foreground_light,
                           const enum EscapeColor background_color,
                           const bool background_light
) {
        set_foreground_color(color_code, foreground_color);
        set_background_color(color_code, background_color);

        const uint8_t light_colors_mask = foreground_light << 3 | background_light << 7;

        *color_code |= light_colors_mask;
}

static inline bool is_foreground_light_palette(const uint8_t *color_escape_sequence) {
        if (color_escape_sequence[2] - 0x30 == 9) {
                return true;
        }

        return false;
}

static inline bool is_background_light_palette(const uint8_t *color_escape_sequence) {
        if (color_escape_sequence[5] - 0x30 == 1) {
                return true;
        }

        return false;
}

static inline ByteColorCode decode_escape_colors(const uint8_t *color_escape_sequence) {
        const bool foreground_light = is_foreground_light_palette(color_escape_sequence);
        const bool background_light = is_background_light_palette(color_escape_sequence);

        const uint8_t foreground_color = color_escape_sequence[3] - 0x30;
        uint8_t background_color = 0;

        if (background_light) {
                background_color = color_escape_sequence[7] - 0x30;
        }
        else {
                background_color = color_escape_sequence[6] - 0x30;
        }

        ByteColorCode color_code = 0;
        set_byte_color(&color_code, foreground_color, foreground_light, background_color, background_light);
        return color_code;
}

#endif //OS_ESCAPE_CODES_H
