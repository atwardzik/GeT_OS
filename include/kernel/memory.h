//
// Created by Artur Twardzik on 30/12/2024.
//

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#include <stdint.h>

extern uint8_t __heap_start__[];
extern uint8_t __heap_end__[];

static uint8_t *const heap_start_ptr = __heap_start__;
static uint8_t *const heap_end_ptr = __heap_end__;

void *kmalloc(size_t size);

void *sys_malloc(size_t size);

void *krealloc(void *ptr, size_t new_size);

void *sys_realloc(void *ptr, size_t new_size);

void kfree(void *ptr);

void sys_free(void *ptr);

size_t get_allocated_size(void);

size_t get_current_heap_size(void);

#endif //MEMORY_H
