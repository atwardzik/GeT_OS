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

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static constexpr size_t INITIAL_PROCESS_COUNT = 20;

static constexpr int DEFAULT_PROCESS_STACK_SIZE = 2 * 1024;

static constexpr off_t DEFAULT_PROCESS_SP_OFFSET = 1 * 1024;

static constexpr int IDLE_PROCESS_STACK_SIZE = 1024;

static constexpr off_t IDLE_PROCESS_SP_OFFSET = 512;

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


struct Pipe_Inode {
        struct VFS_Inode inode;
        void *ptr;
        struct File file;

        int read_offset;
        int write_offset;

        struct Process *wait_for_read;
};


static struct {
        size_t max_processes;
        size_t processes_count;
        struct Process *processes;
        struct Process *current_process;

        size_t total_allocated_memory;

        void *main_kernel_stack;

        struct Process process_idle;
} scheduler __attribute__((section(".data")));

constexpr pid_t PID_IDLE = 0xffff;
constexpr pid_t PID_INIT = 1;

static void idle(void) {
        while (1) {
                __asm__("wfi");
        }
}

extern void action_ignore(int);

static int create_idle_process() {
        void *process_page = kmalloc(IDLE_PROCESS_STACK_SIZE);
        if (!process_page) {
                return -ENOMEM;
        }

        void *kstack = process_page + IDLE_PROCESS_STACK_SIZE - sizeof(size_t);
        void *pstack = process_page + IDLE_PROCESS_SP_OFFSET - sizeof(size_t);
        create_process_stack_frame(&pstack,
                                   &exit,
                                   &idle,
                                   EXC_RETURN_THREAD_PSP_CODE);

        struct Process process = {
                .pid = PID_IDLE,
                .pgid = 0,
                .pname = "IDLE",
                .pstate = READY,
                .priority_level = 0,
                .kernel_mode = false,

                .ppage = nullptr,
                .stack_page_ptr = process_page,
                .pstack = pstack,
                .kstack = kstack,
                .allocated_memory = IDLE_PROCESS_STACK_SIZE,

                .parent = nullptr,
                .max_children_count = 0,
                .children_count = 0,
                .children = nullptr,

                .signal_mask = 0,
                .pending_signals = nullptr,
                .signal_handled = false,
        };
        for (int i = 0; i < 32; ++i) {
                process.sighandlers[i] = action_ignore;
        }
        process.signal_mask = 0xffff'ffff;

        scheduler.process_idle = process;

        return PID_IDLE;
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
                processes[i] = (struct Process){0};
        }

        scheduler.total_allocated_memory = 0;
        scheduler.current_process = nullptr;

        scheduler.main_kernel_stack = current_main_kernel_stack;
        create_idle_process();

        scheduler.current_process = &scheduler.processes[0];

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

static bool scheduler_can_populate_process(void) {
        if (scheduler.processes_count <= scheduler.max_processes) {
                return true;
        }

        return false;
}

static int scheduler_populate_process(const struct Process *process) {
        if (!scheduler_can_populate_process()) {
                return -ENOMEM;
        }

        for (int i = 0; i < scheduler.max_processes; ++i) {
                if (memcmp(&scheduler.processes[i], &(struct Process){0}, sizeof(struct Process)) == 0) {
                        scheduler.processes[i] = *process;
                        break;
                }
        }

        scheduler.processes_count += 1;
        scheduler.total_allocated_memory += process->allocated_memory;
        return 0;
}

static struct Process *scheduler_get_process_by_pid(const pid_t pid) {
        for (int i = 0; i < scheduler.max_processes; ++i) {
                if (scheduler.processes[i].pid == pid) {
                        return &scheduler.processes[i];
                }
        }

        return nullptr;
}

static int scheduler_evaporate_process(const pid_t pid) {
        struct Process *process = scheduler_get_process_by_pid(pid);
        if (process) {
                *process = (struct Process){0};
                scheduler.processes_count -= 1;
                return 0;
        }

        return -1;
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
                        current_index = 0;
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

        off_t msplim_offset = 0;
switch_to_userspace:
        process->pstate = RUNNING;
        msplim_offset = (process->pid == PID_IDLE ? IDLE_PROCESS_SP_OFFSET : DEFAULT_PROCESS_SP_OFFSET) + 8;
        __asm__("msr    psplim, %0\n\r" : : "r"(process->stack_page_ptr));
        __asm__("msr    msplim, %0\n\r" : : "r"(process->stack_page_ptr + msplim_offset));
        __asm__("msr    psp, %0\n\r" : : "r"(process->pstack));
        __asm__("msr    msp, %0\n\r" : : "r"(process->kstack));
        RECALL_USERSPACE_STATE

switch_to_kernelspace:
        msplim_offset = (process->pid == PID_IDLE ? IDLE_PROCESS_SP_OFFSET : DEFAULT_PROCESS_SP_OFFSET) + 8;
        __asm__("msr    psplim, %0\n\r" : : "r"(process->stack_page_ptr));
        __asm__("msr    msplim, %0\n\r" : : "r"(process->stack_page_ptr + msplim_offset));
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
        if (!scheduler_can_populate_process()) {
                return nullptr;
        }

        void *process_page = kmalloc(DEFAULT_PROCESS_STACK_SIZE);
        if (!process_page) {
                return nullptr;
        }

        void *kstack = process_page + DEFAULT_PROCESS_STACK_SIZE - sizeof(size_t);
        void *pstack_begin = process_page + DEFAULT_PROCESS_SP_OFFSET - sizeof(size_t);
        const size_t offset = process_stack_init_argv(pstack_begin, argv);
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
                .pgid = 0,
                .pname = argv[0],
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

        if (scheduler_populate_process(&process) == 0) {
                struct Process *added_process = scheduler_get_process_by_pid(pid);
                pid += 1;
                return added_process;
        }
        return nullptr;
}

static pid_t fill_process_and_spawn(
        struct Process *new_process,
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[], char *const envp[]
) {
        struct Process *current_process = scheduler.current_process;

        struct File **fdtable = kmalloc(sizeof(struct File *) * MAX_OPEN_FILE_DESCRIPTORS);
        if (!fdtable) {
                kfree(new_process);
                return -1;
        }
        struct Process **children = kmalloc(sizeof(struct Process *) * (current_process->max_children_count - 1));
        if (!children) {
                kfree(fdtable);
                kfree(new_process);
                return -1;
        }


        const struct Files files = {.count = current_process->files.count, .fdtable = fdtable};
        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                files.fdtable[i] = current_process->files.fdtable[i];
        }
        memset(children, 0, sizeof(struct Process *) * (current_process->max_children_count - 1));


        new_process->files = files;
        new_process->root = current_process->root;
        new_process->pwd = current_process->pwd;
        new_process->parent = current_process;
        new_process->pgid = current_process->pgid;
        new_process->max_children_count = current_process->max_children_count - 1;
        new_process->children = children;

        for (size_t i = 0; i < current_process->max_children_count; ++i) {
                if (current_process->children[i] == nullptr) {
                        current_process->children[i] = new_process;
                        current_process->children_count += 1;

                        break;
                }
        }

        new_process->pstate = READY;
        return new_process->pid;
}

pid_t sys_spawnp_process(
        void (*process_entry_ptr)(void),
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[], char *const envp[]
) {
        struct Process *new_process = create_blank_process(process_entry_ptr, nullptr, argv);
        if (!new_process) {
                return -1;
        }

        return fill_process_and_spawn(new_process, file_actions, attrp, argv, envp);
}

pid_t sys_spawn_process(
        int fd,
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[],
        char *const envp[]
) {
        const struct Process *current_process = scheduler.current_process;
        const size_t executable_size = current_process->files.fdtable[fd]->f_inode->i_size;
        char *executable_buffer = kmalloc(executable_size);
        if (!executable_buffer) {
                return -1;
        }

        sys_read(fd, executable_buffer, executable_size);

        struct ProcessPage *ppage = load_exec(executable_buffer);
        kfree(executable_buffer);
        if (!ppage || !ppage->page_ptr) {
                return -ENOEXEC;
        }


        void *_start_address = (uint8_t *) ppage->page_ptr + ppage->_start_offset;

        struct Process *new_process = create_blank_process(_start_address, ppage->static_base, argv);
        if (!new_process) {
                return -1;
        }
        new_process->ppage = ppage;
        if (ppage->static_base) {
                new_process->allocated_memory += ppage->static_base_len;
        }
        new_process->allocated_memory += ppage->page_size;

        return fill_process_and_spawn(new_process, file_actions, attrp, argv, envp);
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
        stdin_fop->ioctl = tty_result->inode->i_fop->ioctl;

        struct FileOperations *stdout_fop = kmalloc(sizeof(*stdin_fop));
        *stdout_fop = empty_fop;
        stdout_fop->write = tty_result->inode->i_fop->write;
        stdout_fop->ioctl = tty_result->inode->i_fop->ioctl;

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
        if (scheduler.processes_count) {
                return -1;
        }

        char *argv[] = {"init", "test", nullptr};
        struct Process *process_init = create_blank_process(process_entry_ptr, nullptr, argv);
        if (!process_init) {
                return -1;
        }

        struct Process **children = kmalloc(sizeof(struct Process *) * (INITIAL_PROCESS_COUNT - 1));
        if (!children) {
                kfree(process_init);
                return -1;
        }
        memset(children, 0, sizeof(struct Process *) * (process_init->max_children_count - 1));

        process_init->files = setup_init_stdio(root);
        process_init->root = root;
        process_init->pwd = root;
        process_init->max_children_count = INITIAL_PROCESS_COUNT - 1;
        process_init->children = children;

        process_init->pstate = READY;
        return 0;
}

bool has_free_fd(struct Process *process) {
        if (process->files.count >= MAX_OPEN_FILE_DESCRIPTORS) {
                return false;
        }

        return true;
}

int populate_file(struct Process *process, struct File *file) {
        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                if (process->files.fdtable[i] == nullptr) {
                        process->files.fdtable[i] = file;
                        process->files.count += 1;

                        return i;
                }
        }

        return -1;
}

static ssize_t pipe_read(struct File *file, void *buf, size_t count, off_t file_offset) {
        struct Pipe_Inode *pipe = (struct Pipe_Inode *) file->f_inode;

        while (pipe->write_offset <= pipe->read_offset || pipe->wait_for_read) {
                struct Process *process = scheduler_get_current_process();
                process->pstate = WAITING_FOR_RESOURCE;
                if (!pipe->wait_for_read) {
                        pipe->wait_for_read = process;
                }

                // fixme: add waiting queue
                // due to some reasons unbeknown to me, after this function call
                // and after waking up (when the process is READY) the condition passes
                // and the instruction after loop is executed
                save_kernelmode_and_context_switch();
        }

        const size_t to_read = file_offset + count <= pipe->inode.i_size ? count : pipe->inode.i_size;
        memcpy(buf, pipe->ptr + file_offset, to_read);
        pipe->read_offset = file_offset + count;

        return to_read;
}

static ssize_t pipe_write(struct File *file, void *buf, size_t count, off_t file_offset) {
        struct Pipe_Inode *pipe = (struct Pipe_Inode *) file->f_inode;

        if (file_offset + count > pipe->inode.i_size) {
                void *ptr = krealloc(pipe->ptr, file_offset + count);
                if (!ptr) {
                        return -ENOMEM;
                }
                pipe->ptr = ptr;
                pipe->inode.i_size = file_offset + count;
        }

        memcpy(pipe->ptr + file_offset, buf, count);
        pipe->write_offset = file_offset + count;

        if (pipe->wait_for_read) {
                pipe->wait_for_read->pstate = READY;
                pipe->wait_for_read = nullptr;
        }

        return count;
}

static int pipe_close(struct File *file) {
        const struct Process *current = scheduler.current_process;
        if (file->f_inode && file->f_owner == current->pid) { //FIXME: what about children of the dead process?
                struct Pipe_Inode *pipe_inode = (struct Pipe_Inode *) file->f_inode;
                kfree(pipe_inode->inode.i_fop);
                kfree(pipe_inode->ptr);
                kfree(pipe_inode);
                pipe_inode = nullptr;
        }
}

int sys_pipe(int pipefd[2]) {
        struct Process *current = scheduler.current_process;
        if (current->files.count + 2 > MAX_OPEN_FILE_DESCRIPTORS) {
                printk("[!] Too much files opened.\n");
                return -EMFILE;
        }


        struct Pipe_Inode *pipe_inode = kmalloc(sizeof(*pipe_inode));
        if (!pipe_inode) {
                return -ENOMEM;
        }
        memset(pipe_inode, 0, sizeof(*pipe_inode));

        struct FileOperations *pipe_op = kmalloc(sizeof(*pipe_op));
        if (!pipe_op) {
                return -ENOMEM;
        }
        memset(pipe_op, 0, sizeof(*pipe_op));
        pipe_op->write = pipe_write;
        pipe_op->read = pipe_read;
        pipe_op->close = pipe_close;
        pipe_inode->inode.i_fop = pipe_op;

        struct File file = {0};
        file.f_op = pipe_op;
        file.f_inode = (struct VFS_Inode *) pipe_inode;
        file.f_owner = current->pid;

        pipe_inode->file = file;


        struct File *file_read = kmalloc(sizeof(*file_read));
        if (!file_read) {
                return -ENOMEM;
        }
        *file_read = file;

        struct File *file_write = kmalloc(sizeof(*file_write));
        if (!file_write) {
                return -ENOMEM;
        }
        *file_write = file;

        const int fd_read = populate_file(current, file_read);
        const int fd_write = populate_file(current, file_write);

        pipefd[0] = fd_read;
        pipefd[1] = fd_write;

        return 0;
}

pid_t sys_getpid(void) {
        const struct Process *current = scheduler.current_process;

        return current->pid;
}

pid_t sys_getppid(void) {
        const struct Process *current = scheduler.current_process;

        return current->parent->pid;
}

pid_t sys_getpgid(pid_t pid) {
        const struct Process *current = scheduler.current_process;

        if (pid == 0) {
                return current->pgid;
        }

        for (int i = 0; i < current->children_count; ++i) {
                if (current->children[i]->pid == pid) {
                        return current->children[i]->pgid;
                }
        }

        return -ESRCH;
}

int sys_setpgid(const pid_t pid, const pid_t pgid) {
        struct Process *current = scheduler.current_process;

        if (pid == 0) {
                current->pgid = pgid;
                return 0;
        }

        for (int i = 0; i < current->children_count; ++i) {
                if (current->children[i]->pid == pid) {
                        current->children[i]->pgid = pgid;
                        return 0;
                }
        }

        return -ESRCH;
}

void sys_exit(int status) {
        struct Process *current = scheduler.current_process;
        current->pstate = TERMINATED;
        current->exit_code = status;

        sys_kill(current->pid, SIGKILL);

        __asm__("msr    msp, %0\n\r" : : "r"(scheduler.main_kernel_stack));
        context_switch();
}

void sys_kill(const pid_t pid, int sig) {
        struct Process *process = scheduler_get_process_by_pid(pid);
        if (!process) {
                return;
        }

        if (sig == SIGTERM) {
                signal_notify(process, SIGTERM);
                return;
        }
        // TODO: SEND OTHER SIGNALS

        signal_notify(process->parent, SIGCHLD);
        for (size_t i = 0; i < process->children_count; ++i) {
                signal_notify(process->children[i], SIGHUP);
        }

        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                struct File *file = process->files.fdtable[i];

                if (file && file->f_owner == process->pid) { //FIXME: what about the children of the dead process?
                        if (file->f_op->close) {
                                file->f_op->close(file);
                        }
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
        kfree(process->stack_page_ptr);
        scheduler.total_allocated_memory -= process->allocated_memory;
        process->allocated_memory = 0;

        process->pstate = TERMINATED;
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


        int terminated_child = -1;
        for (size_t i = 0; i < current->max_children_count; ++i) {
                if (current->children[i] && current->children[i]->pstate == TERMINATED) {
                        terminated_child = i;
                }
        }

        if (stat_loc) {
                *stat_loc = current->children[terminated_child]->exit_code;
        }

        //evaporate zombie process
        const pid_t dead_process = current->children[terminated_child]->pid;
        scheduler_evaporate_process(dead_process);
        current->children_count -= 1;
        current->children[terminated_child] = nullptr;

        return dead_process;
}

void run_process_init(void) {
        scheduler.current_process = &scheduler.processes[0];
        scheduler.current_process->pstate = RUNNING;
        __asm__("movs   r0, #1\n\r"  // run init (pid=1)
                "svc    #0xff\n\r"); // OS_INIT_SVC
}
