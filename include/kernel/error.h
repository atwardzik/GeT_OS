//
// Created by Artur Twardzik on 01/04/2026.
//

#ifndef OS_ERROR_H
#define OS_ERROR_H

#include <stdint.h>

#include "errno.h"

#define UNIMPLEMENTED(msg) kernel_panic("Unimplemented feature: " msg, \
                                        __FILE__, __LINE__, __func__)

static inline void *ERR_PTR(uint32_t error) {
        return (void *) error;
}

static inline bool IS_ERR(const void *ptr) {
        return (uint32_t) ptr >= (uint32_t) -MAX_ERRNO || (uint32_t) ptr <= (uint32_t) MAX_ERRNO;
}

[[noreturn]] void kernel_panic(const char *msg, const char *file, int line, const char *func);

#endif //OS_ERROR_H
