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

static inline void set_foreground_color(ByteColorCode *color_code, const enum EscapeColor color, const bool light) {
        *color_code &= ~FOREGROUND_COLOR_BITS;
        *color_code |= color & FOREGROUND_COLOR_BITS;
        *color_code |= light << 3;
}

static inline void set_background_color(ByteColorCode *color_code, const enum EscapeColor color, const bool light) {
        *color_code &= ~BACKGROUND_COLOR_BITS;
        *color_code |= (color << 4) & BACKGROUND_COLOR_BITS;
        *color_code |= light << 7;
}

#endif //OS_ESCAPE_CODES_H
