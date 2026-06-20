//
// Created by Artur Twardzik on 19/11/2025.
//

#ifndef LIBC_H
#define LIBC_H

#include "types.h"

#include <stdarg.h>

#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_APPEND        0x0008
#define O_CREAT         0x0200
#define O_TRUNC         0x0400
#define O_BINARY        0x10000
#define O_DIRECTORY     0x200000

#define	S_IFMT          0170000	 /* type of file */
#define S_IFDIR	        0040000	 /* directory */
#define	S_IFCHR	        0020000	 /* character special */
#define	S_IFBLK	        0060000	 /* block special */
#define	S_IFREG	        0100000	 /* regular */
#define	S_IFLNK	        0120000	 /* symbolic link */
#define	S_IFSOCK        0140000 /* socket */
#define	S_IFIFO	        0010000	 /* fifo */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define	SIGHUP	1	/* hangup */
#define	SIGINT	2	/* interrupt */
#define	SIGQUIT	3	/* quit */
#define	SIGILL	4	/* illegal instruction (not reset when caught) */
#define	SIGTRAP	5	/* trace trap (not reset when caught) */
#define	SIGIOT	6	/* IOT instruction */
#define	SIGABRT 6	/* used by abort, replace SIGIOT in the future */
#define	SIGEMT	7	/* EMT instruction */
#define	SIGFPE	8	/* floating point exception */
#define	SIGKILL	9	/* kill (cannot be caught or ignored) */
#define	SIGBUS	10	/* bus error */
#define	SIGSEGV	11	/* segmentation violation */
#define	SIGSYS	12	/* bad argument to system call */
#define	SIGPIPE	13	/* write on a pipe with no one to read it */
#define	SIGALRM	14	/* alarm clock */
#define	SIGTERM	15	/* software termination signal from kill */
#define	SIGURG	16	/* urgent condition on IO channel */
#define	SIGSTOP	17	/* sendable stop signal not from tty */
#define	SIGTSTP	18	/* stop signal from tty */
#define	SIGCONT	19	/* continue a stopped process */
#define	SIGCHLD	20	/* to parent on child stop or exit */
#define	SIGCLD	20	/* System V name for SIGCHLD */
#define	SIGTTIN	21	/* to readers pgrp upon background tty read */
#define	SIGTTOU	22	/* like TTIN for output if (tp->t_local&LTOSTOP) */
#define	SIGIO	23	/* input/output possible signal */
#define	SIGPOLL	SIGIO	/* System V name for SIGIO */
#define	SIGXCPU	24	/* exceeded CPU time limit */
#define	SIGXFSZ	25	/* exceeded file size limit */
#define	SIGVTALRM 26	/* virtual time alarm */
#define	SIGPROF	27	/* profiling time alarm */
#define	SIGWINCH 28	/* window changed */
#define	SIGLOST 29	/* resource lost (eg, record-lock lost) */
#define	SIGUSR1 30	/* user defined signal 1 */
#define	SIGUSR2 31	/* user defined signal 2 */
#define NSIG	32      /* signal 0 implied */

extern int optind;
extern const char *optargs;

/*
 * Syscalls
 */

typedef struct SpawnFileActions spawn_file_actions_t;
typedef struct SpawnAttr spawnattr_t;

struct timespec {
        time_t tv_sec;
        long tv_nsec;
};

struct stat {
        mode_t st_mode;
        uid_t st_uid;
        gid_t st_gid;
        struct timespec st_mtim;
        size_t st_size;
};

struct DirectoryEntry;
typedef char *caddr_t;
typedef typeof(void (int)) *sighandler_t;

void exit(int code);


/* File syscalls */

int write(int file, const void *buf, int len);

int read(int file, void *buf, int len);

int open(const char *name, int flags, int mode);

int close(int file);

int fstat(int file, struct stat *st);

int readdir(int dirfd, struct DirectoryEntry *directory_entry);

int chdir(const char *path);

int lseek(int file, int offset, int whence);

char *getcwd(char *buf, unsigned int len);


/* Process Syscalls */
pid_t spawnp(
        void (*process_entry_ptr)(void),
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[],
        char *const envp[]
);

pid_t spawn(
        int fd,
        const spawn_file_actions_t *file_actions,
        const spawnattr_t *attrp,
        char *const argv[],
        char *const envp[]
);

int kill(int pid, int sig);

void sigreturn(void);

sighandler_t signal(int signum, sighandler_t handler);

pid_t wait(int *stat_loc);

/* Network Syscalls */
struct in_addr {
        uint32_t s_addr;
};

struct sockaddr_in {
        int16_t sin_family;
        uint16_t sin_port;
        struct in_addr sin_addr;
        char sin_zero[8];
};

struct sockaddr {
        uint16_t sa_family;
        char sa_data[14];
};


int socket(int domain, int type, int protocol);

int bind(int sockfd, const struct sockaddr *addr, size_t addrlen);

int listen(int sockfd, int backlog);

int accept(int sockfd, struct sockaddr *addr, size_t addrlen);

int connect(int sockfd, const struct sockaddr *addr, size_t adrlen);


/*
 * string
 */

unsigned int strlen(const char *str);

int puts(const char *str);

unsigned int strcspn(const char *str, const char *delims);

unsigned int strspn(const char *str, const char *src);

char *strtok(char *str, const char *delim);

char *strcpy(char *dst, const char *src);

char *strcat(char *dst, const char *src);

int strcmp(const char *s1, const char *s2);

int strcasecmp(const char *s1, const char *s2);

int strncmp(const char *s1, const char *s2, unsigned int n);

int strncasecmp(const char *s1, const char *s2, unsigned int n);

char *strchr(const char *str, int ch);

char *strrchr(const char *str, int ch);

char *strstr(const char *haystack, const char *needle);

char *itoa(uint32_t value, char *const str, const unsigned int base);

unsigned long strtoul(const char *str, char **str_end, int base);

int getopt(int argc, char *const argv[], const char *optstring);

static inline bool isprint(const int c) {
        if (c == '\r' || c == '\t' || c == '\n' || c == '\b') {
                return true;
        }

        if (c >= 0x20 && c <= 0xff) {
                return true;
        }

        return false;
}

static inline char tolower(const char c) {
        if (c >= 65 && c <= 90) {
                return c + 32;
        }

        return c;
}


/*
 * memory
 */

void *memset(void *dest, int ch, unsigned int count);

void *memcpy(void *dest, const void *src, unsigned int count);

void *memmove(void *dst, const void *src, size_t len);

int memcmp(const void *dest, const void *src, unsigned int count);

void *malloc(size_t size);

void *realloc(void *ptr, size_t new_size);

void free(void *ptr);


/*
 * printf
 */

int vdprintf(int fd, const char *format, va_list vlist);

int vsnprintf(char *str, size_t size, const char *format, va_list vlist);

int snprintf(char *str, size_t size, const char *format, ...);

int printf(const char *format, ...);

int dprintf(int fd, const char *format, ...);


/*
 * networking
 */

uint16_t htons(uint16_t hostshort);

int inet_aton(const char *host_address, struct in_addr *inp);

#endif // LIBC_H
