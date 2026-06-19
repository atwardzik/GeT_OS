//
// Created by Artur Twardzik on 31/08/2025.
//

#include "resources.h"

#include "memory.h"
#include "proc.h"

// TODO: resource queue to be implemented...
// TODO: sometime processes are killed before they get the resource
//       in such a condition the waiting queue should remove this process
//       as soon as possible

struct wait_queue_entry {
        struct Process *waiting_process;
        struct wait_queue_entry *next;
};

static void add_to_wait_queue(wait_queue_head_t *wq_head, struct Process *process) {
        if (!wq_head) {
                return;
        }

        struct wait_queue_entry *new_entry = kmalloc(sizeof(*new_entry));
        new_entry->waiting_process = process;
        new_entry->next = nullptr;

        if (!*wq_head) {
                *wq_head = new_entry;
                return;
        }

        struct wait_queue_entry *entry = *wq_head;
        while (entry->next) {
                entry = entry->next;
        }

        entry->next = new_entry;

        process->pstate = WAITING_FOR_RESOURCE;
}

struct Process *pop_from_wait_queue(wait_queue_head_t *wq_head) {
        if (!wq_head || !*wq_head || !(*wq_head)->waiting_process->stack_page_ptr) {
                return nullptr;
        }

        struct wait_queue_entry *old_head = *wq_head;
        *wq_head = old_head->next;

        struct Process *head_process = old_head->waiting_process;
        kfree(old_head);

        return head_process;
}

struct Process *top_from_wait_queue(wait_queue_head_t *wq_head) {
        if (!wq_head || !*wq_head || !(*wq_head)->waiting_process->stack_page_ptr) {
                return nullptr;
        }

        return (*wq_head)->waiting_process;
}

void wait_event_interruptible(wait_queue_head_t *wq_head, bool (*condition)(void)) {
        struct Process *process = scheduler_get_current_process();
        add_to_wait_queue(wq_head, process);
        process->pstate = WAITING_FOR_RESOURCE;

        while (!condition()) {
                save_kernelmode_and_context_switch();
        }

        process = pop_from_wait_queue(wq_head);
        process->pstate = READY;
}

void wake_up_interruptible(wait_queue_head_t *wq_head) {
        struct Process *head_process = top_from_wait_queue(wq_head);

        if (head_process) {
                head_process->pstate = READY;
        }
}
