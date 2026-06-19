//
// Created by Artur Twardzik on 19/11/2025.
//

#include "libc.h"

#include <limits.h>

#include "syscall_codes.h"

#include <stdarg.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define SYSCALL(syscall_number) __asm__("svc   #" STR(syscall_number) "\n\r");


void exit(int code) { SYSCALL(EXIT_SVC) }

/*
 * process syscalls
 */

pid_t spawnp(
        void (*process_entry_ptr)(void), const spawn_file_actions_t *file_actions, const spawnattr_t *attrp,
        char *const argv[], char *const envp[]
) {
        int ret;
        SYSCALL(SPAWNP_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

pid_t spawn(
        int fd, const spawn_file_actions_t *file_actions, const spawnattr_t *attrp, char *const argv[],
        char *const envp[]
) {
        int ret;
        SYSCALL(SPAWN_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

void sigreturn(void) { SYSCALL(SIGRETURN_SVC) }

sighandler_t signal(int signum, sighandler_t handler) {
        sighandler_t ret;
        SYSCALL(SIGNAL_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

pid_t wait(int *stat_loc) {
        int ret;
        SYSCALL(WAIT_SVC)

        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}


/*
 * file syscalls
 */

int write(int file, const void *buf, int len) {
        int res;

        SYSCALL(WRITE_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(res));

        return res;
}

int read(int file, void *buf, int len) {
        int res;

        SYSCALL(READ_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(res));

        return res;
}

int open(const char *name, int flags, int mode) {
        int ret;
        SYSCALL(OPEN_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int close(int file) {
        int ret;
        SYSCALL(CLOSE_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}


int fstat(int file, struct stat *st) {
        int ret;
        SYSCALL(FSTAT_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int kill(int pid, int sig) {
        int ret;
        SYSCALL(KILL_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int lseek(int file, int ptr, int dir) {
        int ret;
        SYSCALL(LSEEK_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int readdir(int dirfd, struct DirectoryEntry *directory_entry) {
        int ret;
        SYSCALL(READDIR_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int chdir(const char *path) {
        int ret;
        SYSCALL(CHDIR_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

char *getcwd(char *buf, unsigned int len) {
        char *ret;
        SYSCALL(GETCWD_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}


/*
 * network syscalls
 */

int socket(int domain, int type, int protocol) {
        int ret;
        SYSCALL(SOCKET_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int bind(int sockfd, const struct sockaddr *addr, size_t addrlen) {
        int ret;
        SYSCALL(BIND_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int listen(int sockfd, int backlog) {
        int ret;
        SYSCALL(LISTEN_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int accept(int sockfd, struct sockaddr *addr, size_t addrlen) {
        int ret;
        SYSCALL(ACCEPT_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

int connect(int sockfd, const struct sockaddr *addr, size_t adrlen) {
        int ret;
        SYSCALL(CONNECT_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(ret));

        return ret;
}

/*
 * string
 */

unsigned int strlen(const char *str) {
        int len = 0;

        while (*(str + len)) {
                len += 1;
        }

        return len;
}

int puts(const char *str) {
        int len = strlen(str);

        return write(1, str, len);
}

unsigned int strcspn(const char *str, const char *delims) {
        const int delims_len = strlen(delims);
        int len = 0;


        while (*(str + len)) {
                for (int i = 0; i < delims_len; ++i) {
                        if (*(str + len) == delims[i]) {
                                return len;
                        }
                }
                len += 1;
        }

        return len;
}

unsigned int strspn(const char *str, const char *src) {
        const int src_len = strlen(src);
        int len = 0;


        while (*(str + len)) {
                bool contains_src_char = false;
                for (int i = 0; i < src_len; ++i) {
                        if (*(str + len) == src[i]) {
                                contains_src_char = true;
                        }
                }

                if (!contains_src_char) {
                        return len;
                }
                len += 1;
        }

        return len;
}

char *strtok(char *str, const char *delim) {
        static char *token_start = nullptr;
        static char *ptr = nullptr;
        if (str) {
                ptr = str;
        }

        if (!ptr || !*ptr) {
                return nullptr;
        }

        token_start = ptr;
        ptr += strcspn(ptr, delim);

        const int delims_len = strspn(ptr, delim);
        for (int i = 0; i < delims_len; ++i) {
                *ptr = 0;
                ptr += 1;
        }

        return token_start;
}

char *strcpy(char *dst, const char *src) {
        return memcpy(dst, src, strlen(src) + 1);
}

char *strcat(char *dst, const char *src) {
        char *ptr = dst + strlen(dst);

        for (size_t i = 0; i < strlen(src) + 1; ++i) {
                *(ptr + i) = src[i];
        }

        return dst;
}

int strcmp(const char *s1, const char *s2) {
        while (*s1 && (*s1 == *s2)) {
                s1 += 1;
                s2 += 1;
        }

        return *s1 - *s2;
}

int strcasecmp(const char *s1, const char *s2) {
        while (*s1 && (tolower(*s1) == tolower(*s2))) {
                s1 += 1;
                s2 += 1;
        }

        return tolower(*s1) - tolower(*s2);
}

int strncmp(const char *s1, const char *s2, unsigned int n) {
        int i = 0;

        while (s1[i] && (s1[i] == s2[i])) {
                i += 1;

                if (i == n) {
                        return 0;
                }
        }

        return s1[i] - s2[i];
}

int strncasecmp(const char *s1, const char *s2, unsigned int n) {
        int i = 0;

        while (s1[i] && (tolower(s1[i]) == tolower(s2[i]))) {
                i += 1;

                if (i == n) {
                        return 0;
                }
        }

        return tolower(s1[i]) - tolower(s2[i]);
}

char *strchr(const char *str, const int ch) {
        if (!str) {
                return nullptr;
        }

        for (size_t i = 0; i < strlen(str) + 1; ++i) {
                if (str[i] == (char) ch) {
                        return (char *) &str[i];
                }
        }

        return nullptr;
}

char *strstr(const char *haystack, const char *needle) {
        const size_t m = strlen(needle);
        uint64_t R;
        uint64_t pattern_mask[CHAR_MAX + 1];

        if (m == 0) {
                return (char *) haystack;
        }
        if (m > 63) {
                return nullptr;
        }

        R = ~1;

        for (size_t i = 0; i <= CHAR_MAX; ++i) {
                pattern_mask[i] = 0xffff'ffff'ffff'ffff;
        }
        for (size_t i = 0; i < m; ++i) {
                pattern_mask[(unsigned char) needle[i]] &= ~(1UL << i);
        }

        for (size_t i = 0; haystack[i] != '\0'; ++i) {
                R |= pattern_mask[(unsigned char) haystack[i]];
                R <<= 1;

                if (0 == (R & (1UL << m))) {
                        return (char *) ((haystack + i - m) + 1);
                }
        }

        return nullptr;
}

char *strrchr(const char *str, int ch) {
        if (!str) {
                return nullptr;
        }

        const char *end = str + strlen(str);

        while (end >= str) {
                if (*end == (char) ch) {
                        return end;
                }

                end -= 1;
        }

        return nullptr;
}

char *itoa(uint32_t value, char *const str, const unsigned int base) {
        if (value == 0) {
                *str = '0';
                return str;
        }

        int i = 0;
        uint32_t temp_value = value;
        while (temp_value) {
                i += 1;
                temp_value /= base;
        }

        *(str + i) = 0;

        while (value) {
                const unsigned char digit = (value % base);
                if (digit > 9) {
                        *(str + i - 1) = 'A' + (digit - 10);
                }
                else {
                        *(str + i - 1) = digit + 0x30;
                }

                value /= base;
                i -= 1;
        }

        return str;
}

static unsigned int pow(unsigned int base, unsigned int exp) {
        if (exp == 0) {
                return 1;
        }

        return base * pow(base, exp - 1);
}

unsigned long strtoul(const char *str, char **str_end, int base) {
        const int len = strlen(str);
        const char *ptr = str;

        unsigned long value = 0;
        int i = len - 1;
        while (i >= 0) {
                unsigned char digit = *ptr++;
                if (digit >= '0' && digit <= '9') {
                        digit -= 0x30;
                }
                else if (digit >= 'a' && digit <= 'f') {
                        digit = digit - 'a' + 10;
                }
                else if (digit >= 'A' && digit <= 'F') {
                        digit = digit - 'A' + 10;
                }
                else {
                        value = 0;
                        break;
                }

                if (digit < base) {
                        value += pow(base, i) * digit;
                        i -= 1;
                }
                else {
                        value = 0;
                        break;
                }
        }

        if (str_end) {
                *str_end = (char *) ptr;
        }

        return value;
}


int optind = 1;
const char *optargs = nullptr;

/**
 *
 * The option string optstring may contain the following elements: individual characters,
 * and characters followed by a colon to indicate an option argument is to follow.
 *
 * @returns parameter option if present in optstring or -1 if parameters ended
 */
int getopt(int argc, char *const argv[], const char *optstring) {
        static int index = 1;

        if (index == argc) {
                // optind = index;
                index = 1;
                return -1;
        }

        if (argv[index][0] != '-') {
                // optind = index;
                index = 1;
                return -1;
        }

        const char current_parameter = argv[index][1];

        enum { SINGLE, PARAM, NONE } option = NONE;
        for (size_t i = 0; i < strlen(optstring); ++i) {
                if (current_parameter == optstring[i] && optstring[i + 1] == ':') {
                        option = PARAM;
                        break;
                }

                if (current_parameter == optstring[i]) {
                        option = SINGLE;
                        break;
                }
        }

        if (option == SINGLE) {
                index += 1;
        }
        else if (option == PARAM && argv[index + 1]) {
                optargs = argv[index + 1];
                index += 2;
        }
        else if (option == PARAM) {
                // optargs = "?";
                index += 1;
        }
        if (option == NONE) {
                return '?';
        }

        return current_parameter;
}


/*
 * printf
 */

int vdprintf(int fd, const char *format, va_list vlist) {
        if (strcspn(format, "%") == strlen(format)) {
                write(fd, format, strlen(format));
                return 0;
        }

        const char *const format_end = format + strlen(format);

        const char *ptr_begin = format;
        const char *ptr_end = format;
        while (*ptr_end && ptr_end < format_end) {
                ptr_end += strcspn(ptr_begin, "%");

                int len = ptr_end - ptr_begin;
                write(fd, ptr_begin, len);

                ptr_end += 1;
                if (ptr_end == format_end) {
                        break;
                }
                switch (*ptr_end) {
                        case 'c': {
                                int c = va_arg(vlist, int);
                                write(fd, (unsigned char *) &c, 1);
                                break;
                        }
                        case 's': {
                                const char *str = va_arg(vlist, const char *);
                                write(fd, str, strlen(str));
                                break;
                        }
                        case 'i': {
                                char buf[20] = {};
                                int32_t value = va_arg(vlist, int);
                                if (value < 0) {
                                        buf[0] = '-';
                                        value = -value;
                                        itoa(value, buf + 1, 10);
                                }
                                else {
                                        itoa(value, buf, 10);
                                }
                                write(fd, buf, strlen(buf));
                                break;
                        }
                        case 'x': {
                                char buf[20] = {};
                                itoa(va_arg(vlist, uint32_t), buf, 16);
                                write(fd, buf, strlen(buf));
                                break;
                        }
                        default:
                                return -1;
                }
                ptr_end += 1;


                ptr_begin = ptr_end;
        }

        return 0;
}

int vsnprintf(char *str, size_t size, const char *format, va_list vlist) {
        if (strcspn(format, "%") == strlen(format)) {
                if (str) {
                        const size_t to_copy = size > strlen(format) ? strlen(format) : size;
                        memcpy(str, format, to_copy);
                }
                return strlen(format);
        }

        const char *const format_end = format + strlen(format);

        const char *ptr_begin = format;
        const char *ptr_end = format;
        size_t index = 0;
        while (*ptr_end && ptr_end < format_end) {
                ptr_end += strcspn(ptr_begin, "%");

                unsigned int len = ptr_end - ptr_begin;
                size_t available = size - index;
                size_t to_copy = available > len ? len : available;
                memcpy(str + index, ptr_begin, to_copy);
                index += to_copy;
                available -= len;

                if (ptr_end == format_end) {
                        break;
                }
                ptr_end += 1;

                switch (*ptr_end) {
                        case 'c': {
                                int c = va_arg(vlist, int);
                                len = 1;
                                to_copy = available > len ? len : available;
                                memcpy(str + index, (unsigned char *) &c, to_copy);
                                break;
                        }
                        case 's': {
                                const char *appended_str = va_arg(vlist, const char *);
                                len = strlen(appended_str);
                                to_copy = available > len ? len : available;
                                memcpy(str + index, appended_str, to_copy);
                                break;
                        }
                        case 'i': {
                                char buf[21] = {};
                                int32_t value = va_arg(vlist, int32_t);
                                if (value < 0) {
                                        buf[0] = '-';
                                        value = -value;
                                        itoa(value, buf + 1, 10);
                                }
                                else {
                                        itoa(value, buf, 10);
                                }
                                len = strlen(buf);
                                to_copy = available > len ? len : available;
                                memcpy(str + index, buf, to_copy);
                                break;
                        }
                        case 'x': {
                                char buf[21] = {};
                                itoa(va_arg(vlist, uint32_t), buf, 16);
                                len = strlen(buf);
                                to_copy = available > len ? len : available;
                                memcpy(str + index, buf, to_copy);
                                break;
                        }
                        case 0:
                                break;
                        default:
                                return -1;
                }
                ptr_end += 1;
                index += to_copy;


                ptr_begin = ptr_end;
        }

        return index;
}

int snprintf(char *str, size_t size, const char *format, ...) {
        va_list args;
        va_start(args, format);

        const int res = vsnprintf(str, size, format, args);

        va_end(args);
        return res;
}


/**
 * Simple printf allowing %c, %s and %i, %x parameters. Currently unsafe, as the behaviour
 * is undefined with not enough parameters supplied.
 *
 * %c - is an unsigned character, 8 bits long \n
 * %s - is a null-terminated string \n
 * %i - is a 32-bit signed integer \n
 * %x - is a 32-bit unsigned integer, displayed in hexadecimal format \n
 *
 * @param format - formatting string
 * @param ... - parameters
 */
int printf(const char *format, ...) {
        va_list args;
        va_start(args, format);

        const int res = vdprintf(1, format, args);

        va_end(args);
        return res;
}

int dprintf(int fd, const char *format, ...) {
        va_list args;
        va_start(args, format);

        const int res = vdprintf(fd, format, args);

        va_end(args);
        return res;
}


/*
 * memory
 */

void *memcpy(void *dest, const void *src, const unsigned int count) {
        for (unsigned int i = 0; i < count; ++i) {
                *((char *) dest + i) = *((const char *) src + i);
        }

        return dest;
}

void *memmove(void *dst, const void *src, size_t len) {
        char *d = dst;
        const char *s = src;

        if (d == s) {
                return d;
        }
        if ((uintptr_t) s - (uintptr_t) d - len <= -2 * len) {
                return memcpy(d, s, len);
        }

        if (d < s) {
                for (; len; len--) {
                        *d++ = *s++;
                }
        }
        else {
                while (len) {
                        len--;
                        d[len] = s[len];
                }
        }

        return dst;
}

void *memset(void *dest, const int ch, const unsigned int count) {
        for (unsigned int i = 0; i < count; ++i) {
                *((char *) dest + i) = (unsigned char) ch;
        }

        return dest;
}

int memcmp(const void *dest, const void *src, const unsigned int count) {
        for (size_t i = 0; i < count; ++i) {
                const char left = *((const char *) dest + i);
                const char right = *((const char *) src + i);

                if (left != right) {
                        return left - right;
                }
        }

        return 0;
}

void *malloc(size_t size) {
        void *res;

        SYSCALL(MALLOC_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(res));

        return res;
}

void *realloc(void *ptr, size_t new_size) {
        void *res;

        SYSCALL(REALLOC_SVC)
        __asm__("mov    %0, r0\n\r" : "=r"(res));

        return res;
}

void free(void *ptr) {
        SYSCALL(FREE_SVC)
}

/*
 * networking
 */

inline uint16_t htons(uint16_t hostshort) {
        return (hostshort >> 8) | ((hostshort & 0xff) << 8);
}

int inet_aton(const char *host_address, struct in_addr *inp) {
        char octet[4] = {};

        int j = 0;
        int octet_index = 0;
        for (int i = 0; i < 4; ++i) {
                while (host_address[j] && host_address[j] != '.') {
                        octet[octet_index] = host_address[j];

                        j += 1;
                        octet_index += 1;
                }

                if (host_address[j] == '.' || (host_address[j] == 0 && i == 3)) {
                        j += 1;
                        octet[octet_index] = 0;
                        uint8_t o = strtoul(octet, nullptr, 10);
                        ((unsigned char *) &inp->s_addr)[i] = o;
                        octet_index = 0;
                }

                if (host_address[j] == 0 && i < 3) {
                        return 0;
                }
        }

        return 1;
}
