//
// Created by Artur Twardzik on 24/07/2025.
//

#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stdint.h>

void init_keyboard(uint32_t data_pin, uint32_t clock_pin);

void clr_keyboard_buffer(void);

#endif //KERNEL_KEYBOARD_H