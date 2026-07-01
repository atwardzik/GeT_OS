//
// Created by Artur Twardzik on 30/12/2024.
//

#ifndef OS_STDIO_H
#define OS_STDIO_H

#include "fs/file.h"

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

void init_tty(void);

int setup_tty_chrfile(struct VFS_Inode *mount_point);

void write_to_keyboard_buffer(int c);


int printk(const char *ptr);

void printk_status_init(const char *msg);

void printk_status_step(void);

void printk_status_info(const char *msg);

void printk_status_finish(int return_code);


#endif //OS_STDIO_H
