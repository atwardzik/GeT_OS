//
// Created by Artur Twardzik on 02/07/2026.
//

#ifndef OS_ASCII_CHAR_CODES_H
#define OS_ASCII_CHAR_CODES_H

#include <stdint.h>

enum TerminalSpecial {
        EOL             = 0x00,
        ETX             = 0x03,
        EOT             = 0x04,
        BEL             = 0x07,
        BACKSPACE       = 0x08,
        HT              = 0x09,
        ENDL            = 0x0a,
        VT              = 0x0b,
        FF              = 0x0c,
        CARRIAGE_RETURN = 0x0d,
        ESC             = 0x1b,
        EMPTY_SPACE     = 0x20,

        ICH         = 0x1b5b40, // ESC[@
        ARROW_UP    = 0x1b5b41, // ESC[A
        ARROW_DOWN  = 0x1b5b42, // ESC[B
        ARROW_RIGHT = 0x1b5b43, // ESC[C
        ARROW_LEFT  = 0x1b5b44, // ESC[D
        END         = 0x1b5b46, // ESC[F
        HOME        = 0x1b5b48, // ESC[H
        DCH         = 0x1b5b50, // ESC[P
};

#endif //OS_ASCII_CHAR_CODES_H
