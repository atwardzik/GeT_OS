//
// Created by Artur Twardzik on 14/11/2025.
//

#include "memory.h"
#include "network.h"
#include "proc.h"
#include "signal.h"
#include "syscall_codes.h"
#include "fs/file.h"

typedef uint32_t (*syscall_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

syscall_fn syscall_table[] = {
        [EXIT_SVC] = (syscall_fn) sys_exit,
        [SPAWN_SVC] = (syscall_fn) sys_spawn_process,
        [SPAWNP_SVC] = (syscall_fn) sys_spawnp_process,
        [READ_SVC] = (syscall_fn) sys_read,
        [WRITE_SVC] = (syscall_fn) sys_write,
        [OPEN_SVC] = (syscall_fn) sys_open,
        [CLOSE_SVC] = (syscall_fn) sys_close,
        [WAIT_SVC] = (syscall_fn) sys_wait,
        [READDIR_SVC] = (syscall_fn) sys_readdir,
        [CHDIR_SVC] = (syscall_fn) sys_chdir,
        [LSEEK_SVC] = (syscall_fn) sys_lseek,
        [FSTAT_SVC] = (syscall_fn) sys_fstat,
        [GETCWD_SVC] = (syscall_fn) sys_getcwd,


        [KILL_SVC] = (syscall_fn) sys_kill,
        [SIGNAL_SVC] = (syscall_fn) sys_signal,
        [SIGRETURN_SVC] = (syscall_fn) sys_sigreturn,

        [SOCKET_SVC] = (syscall_fn) sys_socket,
        [BIND_SVC] = (syscall_fn) sys_bind,
        [LISTEN_SVC] = (syscall_fn) sys_listen,
        [ACCEPT_SVC] = (syscall_fn) sys_accept,
        [CONNECT_SVC] = (syscall_fn) sys_connect,

        [MALLOC_SVC] = (syscall_fn) sys_malloc,
        [REALLOC_SVC] = (syscall_fn) sys_realloc,
        [FREE_SVC] = (syscall_fn) sys_free,
};
