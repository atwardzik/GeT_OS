//
// Created by Artur Twardzik on 28/05/2026.
//

#ifndef OS_LOADER_H
#define OS_LOADER_H

#include "types.h"
#include "fs/ramfs.h"

#include <stddef.h>

struct ProcessPage {
        void *page_ptr;
        size_t page_size;

        off_t _start_offset;

        void *static_base;
        size_t static_base_len;
        //todo: add leftover memory from process pages to reuse in the heap
};

struct ProcessPage *load_exec(const void *fbytes);

#endif //OS_LOADER_H
