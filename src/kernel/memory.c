//
// Created by Artur Twardzik on 30/12/2024.
//

#include "memory.h"

#include "libc.h"
#include "proc.h"

/*
 * || ptr | size | next  | ---- | RETURNED CHUNK ||
 *     \_____________\__________^                 ^
 *                    \___________________________|
 */
struct Chunk {
        void *ptr;
        size_t size;
        struct Chunk *next_node;
};

static struct {
        struct Chunk *head;
        size_t allocated_size;
        unsigned int allocated_chunks;
        void *heap_start;
} Allocator __attribute__ ((section (".data"))) = {nullptr, 0, 0, heap_start_ptr};

static size_t align_size(const size_t size) {
        // size_t proposed_size = 1;
        // while (proposed_size < size) {
        //         proposed_size *= 2;
        // }
        const size_t proposed_size = size - (size % 4) + 4;

        if (proposed_size <= 8) {
                return 8;
        }

        return proposed_size;
}

static void *align_ptr(void *ptr) {
        if ((size_t) ptr % 4 == 0) {
                return ptr;
        }

        size_t y = (size_t) ptr / 4;

        return (void *) (y * 4 + 4);
}

static void *find_free_chunk_predecessor(size_t size) {
        struct Chunk *chunk = Allocator.head;

        while (chunk->next_node) {
                void *chunk_end = chunk->ptr + chunk->size;

                const void *new_chunk_ptr = align_ptr(chunk_end);
                size_t space_between_chunks = (void *) chunk->next_node - new_chunk_ptr;

                if (space_between_chunks >= size + sizeof(struct Chunk)) {
                        return chunk;
                }
                chunk = chunk->next_node;
        }

        return chunk;
}

void *kmalloc(size_t size) {
        const size_t aligned_size = align_size(size);
        struct Chunk chunk = {nullptr, aligned_size, nullptr};

        if (Allocator.head == nullptr) {
                chunk.ptr = Allocator.heap_start + sizeof(struct Chunk);
                chunk.next_node = nullptr;
                Allocator.head = (struct Chunk *) Allocator.heap_start;
        }
        else {
                struct Chunk *previous_chunk = find_free_chunk_predecessor(aligned_size);
                chunk.next_node = previous_chunk->next_node;
                previous_chunk->next_node = align_ptr(previous_chunk->ptr + previous_chunk->size);
                chunk.ptr = (void *) previous_chunk->next_node + sizeof(struct Chunk);
        }

        const size_t heap_size_after_alloc = get_current_heap_size() + chunk.size + sizeof(struct Chunk);
        if (heap_size_after_alloc >= (size_t) (heap_end_ptr - heap_start_ptr)) {
                return nullptr;
        }

        Allocator.allocated_size += chunk.size;
        Allocator.allocated_chunks += 1;

        *(struct Chunk *) (chunk.ptr - sizeof(struct Chunk)) = chunk;

        return chunk.ptr;
}

void *sys_malloc(size_t size) {
        struct Process *current_process = scheduler_get_current_process();

        //FIXME: preallocate pages in libc so that the code below is valid
#if 0
        int free_heap_page_index = -1;
        for (size_t i = 0; i < 4; ++i) {
                if (current_process->heap_pages[i] == nullptr) {
                        free_heap_page_index = i;
                        break;
                }
        }
        if (free_heap_page_index < 0) {
                return nullptr;
        }

#endif
        const size_t size_before = Allocator.allocated_size;
        void *ptr = kmalloc(size);
        if (!ptr) {
                return nullptr;
        }
        const size_t size_after = Allocator.allocated_size;

        // current_process->heap_pages[free_heap_page_index] = ptr;
        current_process->allocated_memory += size_after - size_before;

        return ptr;
}

static struct Chunk *find_chunk_by_ptr(const void *ptr) {
        if (ptr == nullptr) {
                return nullptr;
        }

        struct Chunk *temp = Allocator.head;
        if (!temp || !temp->ptr) {
                return nullptr;
        }

        while (temp->ptr != ptr && temp->next_node != nullptr) {
                temp = temp->next_node;
        }

        if (temp->ptr != ptr) {
                return nullptr;
        }

        return temp;
}

void *krealloc(void *ptr, size_t new_size) {
        new_size = align_size(new_size);
        struct Chunk *current = find_chunk_by_ptr(ptr);
        //TODO: extend existing block if there is free space between current and next
        if (current->next_node == nullptr) {
                const size_t addend = new_size - current->size;
                current->size += addend;
                Allocator.allocated_size += addend;
                return ptr;
        }

        void *new_ptr = kmalloc(new_size);
        if (!new_ptr) {
                return nullptr;
        }


        memmove(new_ptr, ptr, current->size);

        kfree(ptr);
        return new_ptr;
}

void *sys_realloc(void *ptr, size_t new_size) {
        if (!ptr) {
                return sys_malloc(new_size);
        }

        struct Process *current_process = scheduler_get_current_process();

#if 0
        for (size_t i = 0; i < 4; ++i) {
                if (current_process->heap_pages[i] != ptr) {
                        continue;
                }

                const size_t size_before = Allocator.allocated_size;
                void *new_ptr = krealloc(ptr, new_size);
                const size_t size_after = Allocator.allocated_size;

                if (new_ptr) {
                        current_process->heap_pages[i] = new_ptr;
                        current_process->allocated_memory += size_after - size_before;
                }

                return new_ptr;
        }
#endif
        const size_t size_before = Allocator.allocated_size;
        void *new_ptr = krealloc(ptr, new_size);
        const size_t size_after = Allocator.allocated_size;

        if (new_ptr) {
                current_process->allocated_memory += size_after - size_before;
                return new_ptr;
        }

        return nullptr;
}

void kfree(void *ptr) {
        if (ptr == nullptr) {
                return;
        }

        struct Chunk *temp = Allocator.head;

        if (temp->ptr == ptr) {
                Allocator.head = temp->next_node;

                Allocator.allocated_chunks -= 1;
                Allocator.allocated_size -= temp->size;

                return;
        }

        while (temp->next_node && temp->next_node->ptr != ptr) {
                temp = temp->next_node;
        }

        if (temp->next_node->ptr == ptr) {
                struct Chunk *found_chunk = temp->next_node;
                const size_t found_chunk_size = found_chunk->size;

                struct Chunk *next = found_chunk->next_node;
                temp->next_node = next;

                Allocator.allocated_chunks -= 1;
                Allocator.allocated_size -= found_chunk_size;
        }
}

void sys_free(void *ptr) {
        if (!ptr) {
                return;
        }
        struct Process *current_process = scheduler_get_current_process();

        for (size_t i = 0; i < 4; ++i) {
                if (current_process->heap_pages[i] != ptr) {
                        continue;
                }

                const size_t size_before = Allocator.allocated_size;
                kfree(ptr);
                const size_t size_after = Allocator.allocated_size;

                current_process->heap_pages[i] = nullptr;
                current_process->allocated_memory -= size_before - size_after;
                break;
        }
}

size_t get_allocated_size(void) {
        return Allocator.allocated_size;
}

size_t get_current_heap_size(void) {
        return Allocator.allocated_size + Allocator.allocated_chunks * sizeof(struct Chunk);
}

#ifdef TESTS

#include "tests/unity.h"

constexpr size_t chunk_padding = sizeof(struct Chunk);

void setUp(void) {
        Allocator.heap_start = FIXED_HEAP_START_ADDRESS;
}

void tearDown(void) {
        free(Allocator.heap_start);
}

void setup_global_allocator(void) {
        Allocator.head = nullptr;
        Allocator.allocated_size = 0;
}

void setup_test_chunks_3size_t_empty_between() {
        void *chunk_location = Allocator.heap_start;
        size_t chunk_size = sizeof(size_t);
        void *next_chunk_location = chunk_location + chunk_padding + chunk_size * 4 + chunk_padding;

        struct Chunk chunk = {chunk_location + chunk_padding, chunk_size, next_chunk_location};
        struct Chunk chunk1 = {next_chunk_location + chunk_padding, chunk_size, nullptr};

        *(struct Chunk *) chunk_location = chunk;
        *(struct Chunk *) next_chunk_location = chunk1;

        Allocator.head = chunk_location;
}

void test_align_size(void) {
        TEST_ASSERT_EQUAL_INT(8, align_size(2));
        TEST_ASSERT_EQUAL_INT(8, align_size(5));
        TEST_ASSERT_EQUAL_INT(16, align_size(9));
        TEST_ASSERT_EQUAL_INT(16, align_size(16));
}

void test_align_ptr(void) {
        void *test_r0 = align_ptr((void *) 0x2000'0000);
        void *test_r1 = align_ptr((void *) 0x2000'0001);
        void *test_r2 = align_ptr((void *) 0x2000'0002);
        void *test_r3 = align_ptr((void *) 0x2000'0003);

        TEST_ASSERT_EQUAL_PTR((void *) 0x2000'0000, test_r0);
        TEST_ASSERT_EQUAL_PTR((void *) 0x2000'0004, test_r1);
        TEST_ASSERT_EQUAL_PTR((void *) 0x2000'0004, test_r2);
        TEST_ASSERT_EQUAL_PTR((void *) 0x2000'0004, test_r3);
}

void test_find_free_chunk_predecessor_between(void) {
        setup_global_allocator();
        setup_test_chunks_3size_t_empty_between();

        void *first_chunk_location = Allocator.heap_start;

        TEST_ASSERT_EQUAL_PTR(first_chunk_location, find_free_chunk_predecessor(sizeof(size_t)));
}

void test_find_free_chunk_predecessor_tail(void) {
        setup_global_allocator();
        setup_test_chunks_3size_t_empty_between();

        void *first_chunk_ptr = Allocator.heap_start + chunk_padding;

        void *second_chunk_location = first_chunk_ptr + 4 * sizeof(size_t) + chunk_padding;

        TEST_ASSERT_EQUAL_PTR(second_chunk_location, find_free_chunk_predecessor(4 * sizeof(size_t)));
}

void test_kmalloc_head(void) {
        setup_global_allocator();

        int *test = kmalloc(sizeof(int));
        *test = 10;
        void *expected_address = (void *) Allocator.head + chunk_padding;

        TEST_ASSERT_EQUAL_PTR(expected_address, test);
        TEST_ASSERT_EQUAL_INT(*test, 10);
}

void test_kmalloc_multiple(void) {
        setup_global_allocator();

        int *test = kmalloc(sizeof(int));
        void *expected_address = (void *) Allocator.head + chunk_padding;

        int *test1 = kmalloc(sizeof(int));
        void *expected_address1 = expected_address + align_size(sizeof(int)) + chunk_padding;

        TEST_ASSERT_EQUAL_PTR(expected_address, test);
        TEST_ASSERT_EQUAL_PTR(expected_address1, test1);
}

void test_kmalloc_between(void) {
        setup_global_allocator();
        setup_test_chunks_3size_t_empty_between();

        void *expected_address = find_free_chunk_predecessor(2 * sizeof(size_t)) + chunk_padding
                                 + sizeof(size_t) + chunk_padding;
        void *test = kmalloc(2 * sizeof(size_t));

        TEST_ASSERT_EQUAL_PTR(expected_address, test);
}

void test_kmalloc_tail(void) {
        setup_global_allocator();
        setup_test_chunks_3size_t_empty_between();

        void *expected_address = find_free_chunk_predecessor(4 * sizeof(size_t)) + chunk_padding
                                 + sizeof(size_t) + chunk_padding;
        void *test = kmalloc(4 * sizeof(size_t));

        TEST_ASSERT_EQUAL_PTR(expected_address, test);
}

void test_kmalloc_size_info(void) {
        setup_global_allocator();

        for (size_t i = 0; i < 5; ++i) {
                kmalloc(sizeof(int));
        }


        TEST_ASSERT_EQUAL_INT(5, Allocator.allocated_chunks);
        TEST_ASSERT_EQUAL_INT(5 * align_size(sizeof(int)), Allocator.allocated_size);
}

void test_kfree_nullptr(void) {
        setup_global_allocator();

        kfree(nullptr);
}

void test_kfree_head_single(void) {
        setup_global_allocator();

        void *test = kmalloc(sizeof(size_t));

        kfree(test);

        TEST_ASSERT_EQUAL_PTR((struct Chunk *) nullptr, Allocator.head);
}

void test_kfree_head_multiple(void) {
        setup_global_allocator();

        void *test = kmalloc(sizeof(size_t));
        void *test1 = kmalloc(sizeof(size_t));

        kfree(test);

        TEST_ASSERT_EQUAL_PTR(test1, Allocator.head->ptr);
}

void test_kfree_tail(void) {
        setup_global_allocator();

        void *test = kmalloc(sizeof(size_t));
        void *test1 = kmalloc(sizeof(size_t));

        kfree(test);

        TEST_ASSERT_EQUAL_PTR((struct Chunk *) nullptr, Allocator.head->next_node);
}

void test_kfree_middle(void) {
        setup_global_allocator();

        void *test = kmalloc(sizeof(size_t));
        void *test1 = kmalloc(sizeof(size_t));
        void *test2 = kmalloc(sizeof(size_t));

        kfree(test1);

        TEST_ASSERT_EQUAL_PTR(test2, Allocator.head->next_node->ptr);
}

void test_kfree_multiple(void) {
        setup_global_allocator();

        constexpr size_t alloc_count = 5;

        void *alloc_ptrs[alloc_count];

        for (size_t i = 0; i < alloc_count; ++i) {
                alloc_ptrs[i] = kmalloc(sizeof(size_t));
        }

        kfree(alloc_ptrs[3]);

        struct Chunk *temp = Allocator.head;
        for (size_t i = 0; i < alloc_count - 1; ++i) {
                TEST_ASSERT_NOT_EQUAL(alloc_ptrs[3], temp->ptr);

                temp = temp->next_node;
        }

        TEST_ASSERT_EQUAL_PTR((struct Chunk *) nullptr, temp);
}

void test_kfree_size_info(void) {
        setup_global_allocator();

        void *test = kmalloc(sizeof(size_t));
        void *test1 = kmalloc(sizeof(size_t));
        void *test2 = kmalloc(sizeof(size_t));

        kfree(test1);

        TEST_ASSERT_EQUAL_INT(2, Allocator.allocated_chunks);
        TEST_ASSERT_EQUAL_INT(2 * sizeof(size_t), Allocator.allocated_size);
}

void test_get_current_heap_size(void) {
        setup_global_allocator();

        kmalloc(sizeof(int));
        kmalloc(sizeof(int));

        size_t expected_size = 2 * align_size(sizeof(int)) + 2 * sizeof(struct Chunk);

        TEST_ASSERT_EQUAL_INT(expected_size, get_current_heap_size());
}

int main(void) {
        UNITY_BEGIN();

        RUN_TEST(test_align_size);
        RUN_TEST(test_align_ptr);
        RUN_TEST(test_find_free_chunk_predecessor_between);
        RUN_TEST(test_find_free_chunk_predecessor_tail);
        RUN_TEST(test_kmalloc_head);
        RUN_TEST(test_kmalloc_multiple);
        RUN_TEST(test_kmalloc_between);
        RUN_TEST(test_kmalloc_tail);
        RUN_TEST(test_kmalloc_size_info);
        RUN_TEST(test_kfree_nullptr);
        RUN_TEST(test_kfree_head_single);
        RUN_TEST(test_kfree_head_multiple);
        RUN_TEST(test_kfree_tail);
        RUN_TEST(test_kfree_middle);
        RUN_TEST(test_kfree_multiple);
        RUN_TEST(test_kfree_size_info);
        RUN_TEST(test_get_current_heap_size);

        return UNITY_END();
}

#endif
