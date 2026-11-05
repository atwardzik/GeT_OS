//
// Created by Artur Twardzik on 05/11/2026.
//

#include "tests.h"

#include "test_fs.c"

void run_kernelspace_tests(void) {}

void run_userspace_tests(void) {
        printf("Running automatic verification tests:\n");

        test_FAT16_create_file();
        test_FAT16_delete_file();
        test_FAT16_create_long_file();

        printf("Automatic verification tests done.\n");
}
