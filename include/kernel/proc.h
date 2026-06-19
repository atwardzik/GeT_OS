//
// Created by Artur Twardzik on 30/12/2024.
//

#ifndef PROC_H
#define PROC_H

#include "resources.h"
#include "types.h"
#include "fs/file.h"
#include "loader.h"

#include <stdint.h>


#define EXC_RETURN_THREAD_PSP_CODE ((void *) 0xfffffffd)
/*
 * The context switch routine has to:
 * - Manually stack remaining registers r4-r11 on the Process Stack
 * - Save current task’s PSP to memory
 * - Load next task’s stack pointer and assign it to PSP
 * - Manually unstack registers r4-r11
 * - Call bx 0xfffffffD which makes the processor switch to Unprivileged Handler Mode,
 *   unstack next task’s exception frame and continue on its PC.
 */

enum State {
        NEW                    = 0,
        RUNNING                = 1,
        READY                  = 2,
        WAITING_FOR_RESOURCE   = 3,
        WAITING_FOR_CHILD_EXIT = 4,
        TERMINATED             = 5,
};

struct signal_queue_entry;

typedef struct signal_queue_entry *signal_queue_head_t;

struct owned_inode_queue_entry;

typedef struct owned_inode_queue_entry *owned_inode_head_t;

int add_to_owned_inodes(owned_inode_head_t *head, struct VFS_Inode *inode);

// TODO: by using MPU forbid process to access system resources
struct Process {
        /* General info */

        pid_t pid;
        enum State pstate;
        unsigned int priority_level;
        int exit_code;
        bool kernel_mode;

        /* Allocation info */

        struct ProcessPage *ppage;
        void *stack_page_ptr;
        void *pstack;
        void *kstack;
        size_t allocated_memory;

        /* Files info */

        struct Files files;
        struct VFS_Inode *root;
        struct VFS_Inode *pwd;
        owned_inode_head_t owned_inodes;

        /* Parent and subprocesses */

        struct Process *parent;
        size_t max_children_count;
        size_t children_count;
        struct Process **children;


        /* Signals */
        uint32_t signal_mask;
        signal_queue_head_t pending_signals;
        bool signal_handled;

        void (*sighandlers[32])(int);
};

typedef struct SpawnFileActions spawn_file_actions_t;
typedef struct SpawnAttr spawnattr_t;


int scheduler_init(void *current_main_kernel_stack);

struct Process *scheduler_get_current_process(void);

void scheduler_update_process(void *psp, void *msp);

void create_process_stack_frame(void **initial_sp, void *lr, void *pc, void *exc_return);

/**
 * Performs context switch without saving the current process. Scheduler
 * chooses a new process and switches to it.
 */
[[noreturn]] void context_switch(void);

/**
 * Performs standard context switch, i.e. the current process was in user mode.
 *
 * The context of the current process is saved on it's PSP, scheduler chooses
 * a new process and switches to it.
 */
[[noreturn]] void save_usermode_and_context_switch(void);

/**
 * Similar to standard context switch, but the current process was in kernel mode.
 *
 * The context of the current process is saved on it's MSP, scheduler chooses
 * a new process and switches to it.
 */
[[noreturn]] void save_kernelmode_and_context_switch(void);


pid_t create_process_init(void (*process_entry_ptr)(void), struct VFS_Inode *root);

/**
 * Creates a new process, based on the current process.
 * @param process_entry_ptr execution start pointer
 * @param file_actions
 * @param file_actions settings for copying fdtable
 * @param attrp priority and permissions for spawned process
 * @param argv arguments passed
 * @param envp is an array of pointers to strings, conventionally of the form key=value,
 *             which are passed as the environment of the new program
 * @return positive for successful child creation, negative for error
 */
pid_t sys_spawnp_process(
        void (*process_entry_ptr)(void),
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[], char *const envp[]
);

/**
 * Creates a new process, based on the current process.
 * @param fd file descriptor of a file that holds code for the process to be spawned
 * @param file_actions settings for copying fdtable
 * @param attrp priority and permissions for spawned process
 * @param argv arguments passed
 * @param envp is an array of pointers to strings, conventionally of the form key=value,
 *             which are passed as the environment of the new program
 * @return positive for successful child creation, negative for error
 */
pid_t sys_spawn_process(
        int fd,
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[],
        char *const envp[]
);

// int sys_execve(const char *path, char *const argv[], char *const envp[]);

/**
 * Suspends execution of current process until stat_loc information is available for a terminated child process,
 * or a signal is received (e.g. SIGCHLD, SIGTERM).
 *
 * In the case of a terminated child, performing a wait allows the system to release the resources associated
 * with the child; if a wait is not performed, then the terminated child remains in a "zombie" state.
 *
 * @param stat_loc contains bitwise OR of options. SIGSTOP/SIGCHLD | exit_code
 * @return the process ID of the child to the calling process. Otherwise, a value of -1.
 */
pid_t sys_wait(int *stat_loc);

/**
 * Sets current state to TERMINATED and performs context switch
 *
 * @param status is an exit code that will be passed to the parent
 */
[[noreturn]] void sys_exit(int status);

void sys_kill(pid_t pid, int sig);

void yield(void);

void run_process_init(void);

#endif // PROC_H
