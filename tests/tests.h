//
// Created by Artur Twardzik on 05/11/2026.
//

#ifndef OS_TESTS_H
#define OS_TESTS_H

#include "libc.h"

#define FAIL printf("[TEST] %s @ %s:%i: \x1b[91;49mFail\x1b[0m.\n", __func__, __FILE__, __LINE__);

#define PASS printf("[TEST] %s: \x1b[92;49mOk\x1b[0m.\n", __func__);

#define ASSERT_EQ(x, y) {                                                       \
        if (x == y) {                                                           \
                PASS;                                                           \
        }                                                                       \
        else {                                                                  \
                FAIL;                                                           \
        }                                                                       \
}

#define ASSERT_EQ_MSG(x, y, msg, err_fmt, err_value) {                          \
        puts(msg);                                                              \
        if (x == y) {                                                           \
                printf("\x1b[92;49mOk\x1b[0m.\n");                              \
        }                                                                       \
        else {                                                                  \
                printf("\x1b[91;49mFail\x1b[0m.\n\t");                          \
                printf(err_fmt, err_value);                                     \
                puts("\n");                                                     \
        }                                                                       \
}

void run_kernelspace_tests(void);

void run_userspace_tests(void);

#endif //OS_TESTS_H
