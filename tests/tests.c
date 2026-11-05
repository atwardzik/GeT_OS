//
// Created by Artur Twardzik on 05/11/2026.
//

#include "tests.h"

#include "test_fs.c"

void run_kernelspace_tests(void) {}

void run_userspace_tests(void) {
        printf("Running automatic verification tests for user space:\n");

        test_RAMFS_create_file();
        test_RAMFS_write_file();
        test_RAMFS_delete_file();

        test_FAT16_create_file();
        test_FAT16_write_fle();
        test_FAT16_write_middle_file();
        test_FAT16_delete_file();
        test_FAT16_create_long_file();

        printf("Automatic verification tests for user space done.\n");
}
