//
// Created by Artur Twardzik on 30/12/2024.
//

#ifndef OS_STDIO_H
#define OS_STDIO_H

#include "fs/file.h"

void init_tty(void);

int setup_tty_chrfile(struct VFS_Inode *mount_point);

void write_to_keyboard_buffer(int c);


int printk(const char *ptr);

void printk_status_init(const char *msg);

void printk_status_step(void);

void printk_status_info(const char *msg);

void printk_status_finish(int return_code);


#endif //OS_STDIO_H
