//
// Created by Artur Twardzik on 30/12/2024.
//

#include "proc.h"

#include "errno.h"
#include "libc.h"
#include "loader.h"
#include "memory.h"
#include "signal.h"
#include "tty.h"

//TODO: processes should be better organized than a static array with fixed-position for give pid.

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static constexpr size_t INITIAL_PROCESS_COUNT = 20;

static constexpr int DEFAULT_PROCESS_STACK_SIZE = 2 * 1024;

static constexpr off_t DEFAULT_PROCESS_SP_OFFSET = 1 * 1024;

#define RECALL_STATE_FROM(sp)                           \
        __asm__(                                        \
                "mrs    r0," STR(sp) "\n\r"             \
                "ldmia  r0!, {r2-r11}\n\r"              \
                "mov    lr, r2\n\r"                     \
                "msr    CONTROL, r3\n\r"                \
                "isb\n\r"                               \
                "msr    " STR(sp) ", r0\n\r"            \
                "bx     lr\n\r"                         \
        );

#define RECALL_USERSPACE_STATE RECALL_STATE_FROM(psp)
#define RECALL_KERNELSPACE_STATE RECALL_STATE_FROM(msp)


struct owned_inode_queue_entry {
        struct VFS_Inode *inode;

        struct owned_inode_queue_entry *next;
};

int add_to_owned_inodes(owned_inode_head_t *head, struct VFS_Inode *inode) {
        if (!head) {
                return -1;
        }

        struct owned_inode_queue_entry *new_entry = kmalloc(sizeof(*new_entry));
        if (!new_entry) {
                return -ENOMEM;
        }
        new_entry->inode = inode;
        new_entry->next = nullptr;

        if (!*head) {
                *head = new_entry;
                return 0;
        }

        struct owned_inode_queue_entry *entry = *head;
        while (entry->next) {
                entry = entry->next;
        }

        entry->next = new_entry;

        return 0;
}

int deallocate_owned_inodes(owned_inode_head_t *head) {
        if (!*head) {
                return -1;
        }

        struct owned_inode_queue_entry *entry = *head;
        struct owned_inode_queue_entry *next;
        do {
                next = entry->next;
                kfree(entry->inode);
                kfree(entry);

                entry = next;
        } while (next != nullptr);

        return 0;
}


static struct {
        size_t max_processes;
        size_t processes_count;
        struct Process *processes;
        struct Process *current_process;

        size_t total_allocated_memory;

        void *main_kernel_stack; // TODO: see if it is needed after processes start running

        struct Process process_idle;
} scheduler __attribute__((section(".data")));

constexpr pid_t PID_IDLE = 0xffff;
constexpr pid_t PID_INIT = 1;

static void idle(void) { while (1); }

static int create_idle_process() {
        const int idle_process_size = 64;

        void *process_page = kmalloc(idle_process_size);
        if (!process_page) {
                return -ENOMEM;
        }

        void *kstack = process_page + idle_process_size - sizeof(size_t);
        void *pstack = process_page + (idle_process_size / 2) - sizeof(size_t);
        create_process_stack_frame(&pstack,
                                   &exit,
                                   &idle,
                                   EXC_RETURN_THREAD_PSP_CODE);

        struct Process process = {
                .pid = PID_IDLE,
                .pstate = READY,
                .priority_level = 0,
                .kernel_mode = false,

                .ppage = nullptr,
                .stack_page_ptr = process_page,
                .pstack = pstack,
                .kstack = kstack,
                .allocated_memory = idle_process_size,

                .parent = nullptr,
                .max_children_count = 0,
                .children_count = 0,
                .children = nullptr,

                .signal_mask = 0,
                .pending_signals = nullptr,
                .signal_handled = false,
        };
        init_default_sighandlers(&process);

        scheduler.process_idle = process;
}

int scheduler_init(void *current_main_kernel_stack) {
        scheduler.max_processes = INITIAL_PROCESS_COUNT;
        scheduler.processes_count = 0;

        struct Process *processes = kmalloc(sizeof(struct Process) * INITIAL_PROCESS_COUNT);
        if (!processes) {
                return -ENOMEM;
        }

        scheduler.processes = processes;
        for (size_t i = 0; i < INITIAL_PROCESS_COUNT; ++i) { //TODO: this should be DYNAMIC
                processes[i].stack_page_ptr = nullptr;
        }

        scheduler.total_allocated_memory = 0;
        scheduler.current_process = nullptr;

        scheduler.main_kernel_stack = current_main_kernel_stack;
        create_idle_process();

        return 0;
}

struct Process *scheduler_get_current_process() { return scheduler.current_process; }

void **scheduler_get_current_kernel_stack(void) {
        if (!scheduler.current_process) {
                return nullptr;
        }

        return &scheduler.current_process->kstack;
}

void **scheduler_get_current_process_stack(void) {
        if (!scheduler.current_process) {
                return nullptr;
        }

        return &scheduler.current_process->pstack;
}


static struct Process *scheduler_get_next_process() {
        static size_t current_index = PID_INIT;

        if (scheduler.processes_count == 0) {
                return nullptr;
        }
        else if (scheduler.processes_count == 1) {
                return scheduler.current_process;
        }

        int max_depth_counter = 0;
        do {
                // TODO: determine task importance, also by implementing priority queue. FIXME: this may spinlock
                current_index += 1;
                max_depth_counter += 1;
                if (current_index == scheduler.max_processes) {
                        current_index = PID_INIT;
                }
                if (max_depth_counter == 2 * scheduler.max_processes) {
                        return &scheduler.process_idle;
                }

                const struct Process *current_process = &scheduler.processes[current_index];

                if (current_process->pending_signals) {
                        return &scheduler.processes[current_index];
                }
        } while (scheduler.processes[current_index].pstate != READY);

        return &scheduler.processes[current_index];
}

void scheduler_update_process(void *psp, void *msp) {
        if (!scheduler.current_process->kernel_mode) {
                scheduler.current_process->pstate = READY;
        }

        scheduler.current_process->pstack = psp;
        scheduler.current_process->kstack = msp;
}

bool is_in_kernel_mode() { return scheduler.current_process->kernel_mode; }

void set_kernel_mode_flag() { scheduler.current_process->kernel_mode = true; }

void reset_kernel_mode_flag() { scheduler.current_process->kernel_mode = false; }

__attribute__((optimize("omit-frame-pointer"))) void context_switch(void) {
        // While it might be tempting to "refactor" this code and extract the
        // forthcoming inline assembly it is not the best idea. The compiler
        // CANNOT guarantee (unfortunately) that the current process pointer will
        // be stored in a register. Therefore, the stack pointers must be restored
        // just before context switching. Moreover, there is a hierarchy of
        // handling: handled signals -> pending signals -> kernel mode -> user
        // mode
        register struct Process *process = scheduler_get_next_process();

        scheduler.current_process = process;

        if (process->signal_handled) {
                goto switch_to_userspace;
        }

        int pending_signal = get_pending_signal();
        if (pending_signal == SIGCHLD && process->pstate == WAITING_FOR_CHILD_EXIT) {
                process->pstate = READY;
                goto switch_to_kernelspace;
        }
        else if (pending_signal >= 0 && handle_pending_signal(pending_signal)) {
                goto switch_to_userspace;
        }

        if (process->kernel_mode) {
                goto switch_to_kernelspace;
        }

switch_to_userspace:
        process->pstate = RUNNING;
        __asm__("msr    psplim, %0\n\r" : : "r"(process->stack_page_ptr));
        __asm__("msr    msplim, %0\n\r" : : "r"(process->stack_page_ptr + DEFAULT_PROCESS_SP_OFFSET + 8));
        __asm__("msr    psp, %0\n\r" : : "r"(process->pstack));
        __asm__("msr    msp, %0\n\r" : : "r"(process->kstack));
        RECALL_USERSPACE_STATE

switch_to_kernelspace:
        __asm__("msr    psplim, %0\n\r" : : "r"(process->stack_page_ptr));
        __asm__("msr    msplim, %0\n\r" : : "r"(process->stack_page_ptr + DEFAULT_PROCESS_SP_OFFSET + 8));
        __asm__("msr    psp, %0\n\r" : : "r"(process->pstack));
        __asm__("msr    msp, %0\n\r" : : "r"(process->kstack));
        RECALL_KERNELSPACE_STATE
}

/**
 * Passes arguments to the process stack. TODO: check after implementing MPU if the process doesn't have to own all strings
 * @param pstack initial stack pointer
 * @param argv arguments array ending with nullptr
 * @return bytes written
 */
static size_t process_stack_init_argv(void *pstack, char *const argv[]) {
        if (argv == nullptr) {
                return 0;
        }
        size_t count = 0;
        while (argv[count]) {
                count += 1;
        }
        count += 1; // leave nullptr at the end of argv table

        size_t i = 0;
        while (count) {
                *(size_t *) (pstack - i * 4) = (size_t *) argv[count - 1];

                count -= 1;
                i += 1;
        }

        return i * sizeof(size_t);
}

static struct Process *create_blank_process(void (*process_entry_ptr)(void), void *static_base, char *const argv[]) {
        static pid_t pid = PID_INIT;

        if (pid >= scheduler.max_processes) {
                __asm__("bkpt   #0");
        }

        void *process_page = kmalloc(DEFAULT_PROCESS_STACK_SIZE);
        void *kstack = process_page + DEFAULT_PROCESS_STACK_SIZE - sizeof(size_t);
        void *pstack_begin = process_page + DEFAULT_PROCESS_SP_OFFSET - sizeof(size_t);
        size_t offset = process_stack_init_argv(pstack_begin, argv);
        void *pstack = pstack_begin - offset;
        create_process_stack_frame(&pstack,
                                   &exit,
                                   process_entry_ptr,
                                   EXC_RETURN_THREAD_PSP_CODE);
        // argv
        *(size_t *) (pstack_begin - offset - 28) = (size_t *) (pstack_begin - offset + sizeof(size_t));
        // argc without nullptr at the end
        *(size_t *) (pstack_begin - offset - 32) = (offset - sizeof(size_t)) / sizeof(size_t);

        *(size_t *) (pstack + 28) = (uintptr_t) static_base; //r9 = static base register

        struct Process process = {
                .pid = pid,
                .pstate = NEW,
                .priority_level = 0,
                .kernel_mode = false,

                .ppage = nullptr,
                .stack_page_ptr = process_page,
                .pstack = pstack,
                .kstack = kstack,
                .heap_pages = {},
                .allocated_memory = DEFAULT_PROCESS_STACK_SIZE,

                .parent = nullptr,
                .max_children_count = 0,
                .children_count = 0,
                .children = nullptr,

                .signal_mask = 0,
                .pending_signals = nullptr,
                .signal_handled = false,

        };
        init_default_sighandlers(&process);

        scheduler.total_allocated_memory += process.allocated_memory;
        scheduler.processes[pid] = process;
        scheduler.processes_count += 1;

        struct Process *added_process = &scheduler.processes[pid];
        pid += 1;
        return added_process;
}

pid_t sys_spawnp_process(
        void (*process_entry_ptr)(void),
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[], char *const envp[]
) {
        if (!scheduler.processes[PID_INIT].stack_page_ptr) {
                __asm__("bkpt   #0");
        }

        struct Process *current = scheduler.current_process;
        struct File **fdtable = kmalloc(sizeof(struct File *) * MAX_OPEN_FILE_DESCRIPTORS);
        struct Files files = {current->files.count, fdtable};
        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                files.fdtable[i] = current->files.fdtable[i];
        }

        struct Process *process = create_blank_process(process_entry_ptr, nullptr, argv);
        if (!process->stack_page_ptr) {
                return -1;
        }

        process->files = files;
        process->root = current->root;
        process->pwd = current->pwd;
        process->parent = current;
        process->max_children_count = current->max_children_count - 1;
        process->children = kmalloc(sizeof(struct Process *) * (current->max_children_count - 1));
        for (size_t i = 0; i < process->max_children_count; ++i) {
                process->children[i] = nullptr;
        }

        for (size_t i = 0; i < current->max_children_count; ++i) {
                if (current->children[i] == nullptr) {
                        current->children[i] = process;
                        current->children_count += 1;

                        break;
                }
        }

        process->pstate = READY;
        return process->pid;
}

pid_t sys_spawn_process(
        int fd,
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[],
        char *const envp[]
) {
        if (!scheduler.processes[PID_INIT].stack_page_ptr) {
                __asm__("bkpt   #0");
        }
        struct Process *current = scheduler.current_process;

        const size_t executable_size = current->files.fdtable[fd]->f_inode->i_size;
        char *executable_buffer = kmalloc(executable_size);
        sys_read(fd, executable_buffer, executable_size);
        struct ProcessPage *ppage = load_exec(executable_buffer);
        kfree(executable_buffer);

        if (!ppage || !ppage->page_ptr) {
                return -ENOEXEC;
        }

        void *_start_address = (uint8_t *) ppage->page_ptr + ppage->_start_offset;

        struct Process *process = create_blank_process(_start_address, ppage->static_base, argv);
        if (!process->stack_page_ptr) {
                return -1;
        }
        process->ppage = ppage;
        if (ppage->static_base) {
                process->allocated_memory += ppage->static_base_len;
        }
        process->allocated_memory += ppage->page_size;


        struct File **fdtable = kmalloc(sizeof(struct File *) * MAX_OPEN_FILE_DESCRIPTORS);
        struct Files files = {current->files.count, fdtable};
        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                files.fdtable[i] = current->files.fdtable[i];
        }
        process->files = files;
        process->root = current->root;
        process->pwd = current->pwd;
        process->parent = current;
        process->max_children_count = current->max_children_count - 1;
        process->children = kmalloc(sizeof(struct Process *) * (current->max_children_count - 1));
        for (size_t i = 0; i < process->max_children_count; ++i) {
                process->children[i] = nullptr;
        }

        for (size_t i = 0; i < current->max_children_count; ++i) {
                if (current->children[i] == nullptr) {
                        current->children[i] = process;
                        current->children_count += 1;

                        break;
                }
        }

        process->pstate = READY;
        return process->pid;
}

static struct Files setup_init_stdio(struct VFS_Inode *root) {
        struct Dentry dev = {
                .name = "dev",
        };
        struct Dentry *dev_result = root->i_op->lookup(root, &dev, 0);
        struct Dentry tty = {
                .name = "tty0",
        };
        const struct Dentry *tty_result = dev_result->inode->i_op->lookup(dev_result->inode, &tty, 0);


        const struct FileOperations empty_fop = {};
        struct FileOperations *stdin_fop = kmalloc(sizeof(*stdin_fop));
        *stdin_fop = empty_fop;
        stdin_fop->read = tty_result->inode->i_fop->read;

        struct FileOperations *stdout_fop = kmalloc(sizeof(*stdin_fop));
        *stdout_fop = empty_fop;
        stdout_fop->write = tty_result->inode->i_fop->write;

        struct File *f_stdin = kmalloc(sizeof(*f_stdin));
        f_stdin->f_pos = 0;
        f_stdin->f_op = stdin_fop;
        f_stdin->f_owner = 0;
        f_stdin->f_inode = tty_result->inode;

        struct File *f_stdout = kmalloc(sizeof(*f_stdout));
        f_stdout->f_pos = 0;
        f_stdout->f_op = stdout_fop;
        f_stdout->f_owner = 0;
        f_stdout->f_inode = tty_result->inode;

        struct File *f_stderr = kmalloc(sizeof(*f_stderr));
        f_stderr->f_pos = 0;
        f_stderr->f_op = stdout_fop;
        f_stderr->f_owner = 0;
        f_stderr->f_inode = tty_result->inode;

        struct File **fdtable = kmalloc(sizeof(struct File *) * MAX_OPEN_FILE_DESCRIPTORS);
        memset(fdtable, 0, sizeof(struct File *) * MAX_OPEN_FILE_DESCRIPTORS);
        fdtable[0] = f_stdin;
        fdtable[1] = f_stdout;
        fdtable[2] = f_stderr;

        const struct Files files = {3, fdtable};
        return files;
}


pid_t create_process_init(void (*process_entry_ptr)(void), struct VFS_Inode *root) {
        if (scheduler.current_process || scheduler.processes[PID_INIT].stack_page_ptr) {
                __asm__("bkpt   #0");
        }

        char *argv[] = {"init", "test", nullptr};
        struct Process *process = create_blank_process(process_entry_ptr, nullptr, argv);
        if (!process->stack_page_ptr) {
                __asm__("bkpt   #0");
        }

        process->files = setup_init_stdio(root);
        process->root = root;
        process->pwd = root;
        process->max_children_count = INITIAL_PROCESS_COUNT - 1;
        process->children = kmalloc(sizeof(struct Process *) * (INITIAL_PROCESS_COUNT - 1));
        for (size_t i = 0; i < process->max_children_count; ++i) {
                process->children[i] = nullptr;
        }

        process->pstate = READY;
        return 0;
}


void sys_exit(int status) {
        struct Process *current = scheduler.current_process;
        current->pstate = TERMINATED;
        current->exit_code = status;

        signal_notify(current->parent, SIGCHLD);

        for (size_t i = 0; i < current->children_count; ++i) {
                signal_notify(current->children[i], SIGHUP);
        }

        sys_kill(current->pid, SIGKILL);

        __asm__("msr    msp, %0\n\r" : : "r"(scheduler.main_kernel_stack));
        context_switch();
}

void sys_kill(const pid_t pid, int sig) {
        struct Process *process = &scheduler.processes[pid];
        if (!process->stack_page_ptr) {
                return;
        }

        if (sig == SIGTERM) {
                signal_notify(process, SIGTERM);
                return;
        }

        for (size_t i = 0; i < process->files.count; ++i) {
                struct File *file = process->files.fdtable[i];

                if (file->f_owner == process->pid) {
                        kfree(file);
                }
        }

        if (process->ppage) {
                kfree(process->ppage->page_ptr);
                kfree(process->ppage->static_base);
                kfree(process->ppage);
        }
        for (size_t i = 0; i < 4; ++i) {
                if (process->heap_pages[i]) {
                        // Technically, having to free those pointers means userspace memleak
                        kfree(process->heap_pages[i]);
                }
        }
        kfree(process->files.fdtable);
        kfree(process->children);
        deallocate_signal_queue(&process->pending_signals);
        deallocate_owned_inodes(&process->owned_inodes);
        kfree(process->stack_page_ptr);
        scheduler.total_allocated_memory -= process->allocated_memory;
        process->allocated_memory = 0;

        scheduler.processes[pid].pstate = TERMINATED;
}

pid_t sys_wait(int *stat_loc) {
        struct Process *current = scheduler.current_process;

        current->pstate = WAITING_FOR_CHILD_EXIT;
        bool child_terminated = false;
        for (size_t i = 0; i < current->max_children_count; ++i) {
                if (current->children[i] && current->children[i]->pstate == TERMINATED) {
                        child_terminated = true;
                }
        }
        if (!child_terminated) {
                save_kernelmode_and_context_switch();
        }
        current->pstate = RUNNING;


        pid_t terminated_child = -1;
        for (size_t i = 0; i < current->max_children_count; ++i) {
                if (current->children[i] && current->children[i]->pstate == TERMINATED) {
                        terminated_child = i;
                }
        }
        *stat_loc = current->children[terminated_child]->exit_code;
        const pid_t dead_process = current->children[terminated_child]->pid;

        //evaporate zombie process
        const struct Process p = {};
        *current->children[terminated_child] = p;
        current->children_count -= 1;
        current->children[terminated_child] = nullptr;

        return dead_process;
}

void run_process_init(void) {
        scheduler.current_process = &scheduler.processes[PID_INIT];
        scheduler.current_process->pstate = RUNNING;
        __asm__("movs   r0, #1\n\r"  // run init (pid=1)
                "svc    #0xff\n\r"); // OS_INIT_SVC
}
