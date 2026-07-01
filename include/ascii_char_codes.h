//
// Created by Artur Twardzik on 02/07/2026.
//

#ifndef OS_ASCII_CHAR_CODES_H
#define OS_ASCII_CHAR_CODES_H

#include <stdint.h>

constexpr uint8_t EOL = 0x00;
constexpr uint8_t ETX = 0x03;
constexpr uint8_t EOT = 0x04;

constexpr uint8_t BEL = 0x07;
constexpr uint8_t BACKSPACE = 0x08;
constexpr uint8_t HT = 0x09;
constexpr uint8_t ENDL = 0x0A;
constexpr uint8_t VT = 0x0B;
constexpr uint8_t FF = 0x0C;
constexpr uint8_t CARRIAGE_RETURN = 0x0D;
constexpr uint8_t ESC = 0x1b;
constexpr uint8_t EMPTY_SPACE = 0x20;

constexpr uint32_t ARROW_LEFT = 0x1b5b44;
constexpr uint32_t ARROW_RIGHT = 0x1b5b43;
constexpr uint32_t ARROW_UP = 0x1b5b41;
constexpr uint32_t ARROW_DOWN = 0x1b5b42;

#endif //OS_ASCII_CHAR_CODES_H
