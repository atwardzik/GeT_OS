//
// Created by Artur Twardzik on 28/10/2025.
//

#include "signal.h"

#include "libc.h"
#include "memory.h"

struct signal_queue_entry {
        int pending_signal;

        struct signal_queue_entry *next;
};

static void add_to_signal_queue(signal_queue_head_t *sq_head, const int signal) {
        if (!sq_head) {
                return;
        }

        struct signal_queue_entry *new_entry = kmalloc(sizeof(*new_entry));
        new_entry->pending_signal = signal;
        new_entry->next = nullptr;

        if (!*sq_head) {
                *sq_head = new_entry;
                return;
        }

        struct signal_queue_entry *entry = *sq_head;
        while (entry->next) {
                entry = entry->next;
        }

        entry->next = new_entry;
}

static int pop_from_signal_queue(signal_queue_head_t *sq_head) {
        if (!sq_head || !*sq_head) {
                return -1;
        }

        struct signal_queue_entry *old_head = *sq_head;
        *sq_head = old_head->next;

        const int pending = old_head->pending_signal;
        kfree(old_head);

        return pending;
}

static int top_from_signal_queue(const signal_queue_head_t *sq_head) {
        return (*sq_head)->pending_signal;
}

void deallocate_signal_queue(signal_queue_head_t *sq_head) {
        while (*sq_head) {
                pop_from_signal_queue(sq_head);
        }
}

static void action_terminate(int) {
        // This is only the address, as the killing is done by the kernel
}

void action_ignore(int) {
        //As the name suggests, this ignores the signal.
}


void init_default_sighandlers(struct Process *process) {
        process->sighandlers[SIGTERM] = action_terminate;
        process->sighandlers[SIGKILL] = action_terminate;
        process->sighandlers[SIGINT] = action_terminate;
        process->sighandlers[SIGHUP] = action_terminate;

        process->sighandlers[SIGCHLD] = action_ignore;
}

void signal_notify(struct Process *process, const int sig) {
        if (!(process->signal_mask & (1 << sig))) {
                add_to_signal_queue(&process->pending_signals, sig);
        }
}

int get_pending_signal() {
        struct Process *current_process = scheduler_get_current_process();

        if (current_process->pending_signals) {
                return pop_from_signal_queue(&current_process->pending_signals);
        }

        return -1;
}

bool handle_pending_signal(const int pending_signal) {
        struct Process *current_process = scheduler_get_current_process();

        if (pending_signal < 0 || pending_signal >= 32 || !current_process->sighandlers[pending_signal]) {
                return false;
        }

        void (*current_action)(int) = current_process->sighandlers[pending_signal];

        if (current_action == &action_terminate) {
                //Termination due to an uncaught signal results in exit status 128+[<signal number>]
                sys_exit(128 + pending_signal);
        }
        else if (current_action == &action_ignore) {
                //do nothing
        }
        else {
                current_process->signal_handled = true;
                create_process_stack_frame(&current_process->pstack,
                                           &sigreturn,
                                           current_process->sighandlers[pending_signal],
                                           EXC_RETURN_THREAD_PSP_CODE);
                //signal code for this pending signal must be stored on a stack as r0
                *((uint32_t *) (current_process->pstack + 40)) = pending_signal;

                return true;
        }

        return false;
}

void sys_sigreturn(void) {
        struct Process *current_process = scheduler_get_current_process();
        __asm__("mrs    %0, psp" : "=r"(current_process->pstack));

        current_process->signal_handled = false;
        current_process->pstate = READY;

        // discard registers saved on ISR entry, as the current frame is not important anymore
        current_process->pstack += 40;
        context_switch();
}

sighandler_t sys_signal(int signum, sighandler_t handler) {
        struct Process *current_process = scheduler_get_current_process();

        if (signum < 0 || signum >= 32) {
                return nullptr;
        }

        sighandler_t old_handler = current_process->sighandlers[signum];
        current_process->sighandlers[signum] = handler;

        return old_handler;
}
