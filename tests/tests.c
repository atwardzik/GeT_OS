//
// Created by Artur Twardzik on 05/11/2026.
//

#include "tests.h"

#include "test_fs.c"
#include "test_proc.c"
#include "userspaceVi/visual_editor.h"

void run_kernelspace_tests(void) {}

void run_userspace_tests(void) {
        printf("Running automatic verification tests for user space:\n");

#if 0
        printf("\n[RAMFS]\n");
        test_RAMFS_create_file();
        test_RAMFS_write_file();
        test_RAMFS_delete_file();

        printf("\n[FAT16]\n");
        test_FAT16_create_file();
        test_FAT16_write_fle();
        test_FAT16_write_middle_file();
        test_FAT16_delete_file();
        test_FAT16_create_long_file();
#endif

        printf("\n[PROC]\n");
        test_proc_exec_wait();
        test_proc_multiple_wait();
        test_proc_exec_return();
        test_simple_elf_exec();


        char *params[] = {"vi", "/mnt/disk0/start.s", nullptr};
        [[maybe_unused]] const int vi_pid = spawnp((void (*)(void)) run_editor, nullptr, nullptr, params, nullptr);
        int code;
        const int returned_pid = wait(&code);

        printf("\nAutomatic verification tests for user space done.\n");
}
