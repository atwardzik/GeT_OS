#include "libc.h"
#if 0
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>


extern int printf(const char *fmt, ...);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, void *buf, size_t count);
extern int open(const char *, int, ...);
extern int close(int __fildes);
#endif

void run_program(const char *cmd);

void perform_kill(void);

void echo(void);

void rawecho(void);

int main(void) {
        while (1) {
                puts(" > ");
                char buffer[256];
                if (read(0, buffer, 256) < 1) {
                        continue;
                }
                buffer[strcspn(buffer, "\n")] = 0;

                char *cmd = strtok(buffer, " ");

                if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
                        puts("By default, some crucial executables are stored in /bin and /sbin directories,\n"
                                "they do not require specifying full path. One can ls those directories to\n"
                                "find out what programs are available. For now there is no PATH variable support.\n"
                                "In order to run any scripts/executables use: \n"
                                "\t path/to/exec [parameters]*\n\n"
                                "\tTemporarily available gsh builtin commands:\n"
                                "\t  (h)elp - prints this help screen\n"
                                "\t  (k)ill - kills proc previously set up by kernel with pid=1 (diode)\n"
                                "\t(m)orcik - prints a colorful message\n"
                                "\t  (e)cho - writes to specified file (type echo --h to get more info)\n"
                                "\t rawecho - writes raw hex bytes to specified file NOT SAFE\n"
                                "\t           DO NOT TRY TO PASS NON-HEX BYTES!!!\n"
                                "\t      cd - change current directory\n"
                                "\t     pwd - print working directory\n\n"
                        );
                }
                else if (strcmp(cmd, "kill") == 0 || strcmp(cmd, "k") == 0) {
                        perform_kill();
                }
                else if (strcmp(cmd, "morcik") == 0 || strcmp(cmd, "m") == 0) {
                        const char *text = "Meine beliebte Olga ist die schoenste Frau auf der Welt";
                        const int colors[] = {31, 91, 33, 32, 34, 94, 35};
                        const int num_colors = sizeof(colors) / sizeof(colors[0]);

                        for (size_t i = 0; i < strlen(text); ++i) {
                                const int color = colors[i % num_colors];
                                printf("\x1b[%i;40m%c\x1b[0m", color, text[i]);
                        }

                        puts("\n");
                }
                else if (strcmp(cmd, "echo") == 0 || strcmp(cmd, "e") == 0) {
                        echo();
                }
                else if (strcmp(cmd, "rawecho") == 0) {
                        rawecho();
                }
                else if (strcmp(cmd, "cd") == 0) {
                        const char *path = strtok(nullptr, " ");

                        const int code = chdir(path);
                        if (code == -1) {
                                puts("No such file.\n");
                        }
                }
                else if (strcmp(cmd, "pwd") == 0) {
                        char buf[64];
                        char *ptr = buf;
                        char *ret = getcwd(ptr, 64);

                        puts(ret);
                        puts("\n");
                }
                else {
                        run_program(cmd);
                }
        }
}

void echo(void) {
        // dummy solution for run parameters
        char *argument_line = strtok(nullptr, "");
        int flags = O_WRONLY | O_TRUNC;
        const size_t redirection_index = strcspn(argument_line, ">");
        if (redirection_index < strlen(argument_line)) {
                if (argument_line[redirection_index + 1] == '>') {
                        flags = O_APPEND;
                }
        }

        const char *text = strtok(argument_line, ">");
        if (!text) {
                return;
        }

        const char *path = strtok(nullptr, "");
        int fd = 1;
        if (path) {
                fd = open(path, flags, 0);
                if (fd < 0) {
                        dprintf(2, "No such file.\n");
                        return;
                }
        }


        write(fd, text, strlen(text));
        write(fd, "\n", 1);

        close(fd);
}

void rawecho() {
        char *argument_line = strtok(nullptr, "");
        int flags = O_WRONLY | O_TRUNC;
        const size_t redirection_index = strcspn(argument_line, ">");
        if (redirection_index < strlen(argument_line)) {
                if (argument_line[redirection_index + 1] == '>') {
                        flags = O_APPEND;
                }
        }

        const char *text = strtok(argument_line, ">");
        if (!text) {
                return;
        }

        const char *path = strtok(nullptr, "");
        if (!path) {
                dprintf(2, "No file specified.\n");
                return;
        }
        const int fd = open(path, flags, 0);
        if (fd < 0) {
                dprintf(2, "No such file.\n");
                return;
        }


        char bytes[80] = {};
        memset(bytes, 0, 80);
        int index = 0;

        const char *ptr = text;
        char *endptr;
        while (*ptr) {
                while (*ptr == ' ') {
                        ptr += 1;
                }

                if (*ptr == '\0') {
                        break;
                }

                const char byte[3] = {*ptr, *(ptr + 1), 0};
                const unsigned char value = (unsigned char) strtoul(byte, nullptr, 16);

                bytes[index++] = value;

                ptr += 2;
        }

        write(fd, bytes, index);
        close(fd);
}


void run_program(const char *cmd) {
        // find if the program starts with dot slash
        // find if there is an ampersand to get background processes (memchr)
        // check if the program is an executable (from filesystem flags)
        // dummy solution for run parameters
        const char *program_args[20] = {cmd, nullptr};
        int i = 1;
        const char *arg;
        while ((arg = strtok(nullptr, " ")) != nullptr) {
                if (i == 19) { //the last one MUST be nullptr
                        break;
                }
                program_args[i] = arg;
                i += 1;
        }
        program_args[i] = nullptr;

        int fd = -1;
        const char executable_locations[3][20] = {"./", "bin/", "/mnt/disk0/"};
        for (i = 0; i < 3; ++i) {
                char buf[64] = {};
                const int path_len = strlen(executable_locations[i]);

                memcpy(buf, executable_locations[i], strlen(executable_locations[i]));

                memcpy(buf + path_len, cmd, strlen(cmd));
                fd = open(buf, O_BINARY, 0);
                if (fd > 0) {
                        break;
                }
        }
        if (fd < 0) {
                dprintf(2, "gsh: command not found: %s\n", cmd);
                return;
        }

        spawn(fd, nullptr, nullptr, (char * const *) program_args, nullptr);

        int code;
        const int returned_pid = wait(&code);

        printf("\nChild process %i exited with code: %i\n",
               returned_pid, (char) code);
        close(fd);
}

void perform_kill(void) {
        const char *sig = nullptr;
        const char *process = nullptr;

        const char *param = strtok(nullptr, " ");
        if (strcmp(param, "--h") == 0) {
                puts("kill - terminate or signal a process\n"
                        "Usage:\tkill [-s signal_name] pid\n\n"
                        "Commonly used signals:\n"
                        " - HUP (hang up)\n"
                        " - INT (interrupt)\n"
                        " - QUIT (quit)\n"
                        " - KILL (non-ignorable kill)\n"
                        " - TERM (software termination signal)\n"
                        "By default, without specifying -s parameter, SIGTERM is sent.\n");
                return;
        }
        else if (strcmp(param, "-s") == 0) {
                sig = strtok(nullptr, " ");
                process = strtok(nullptr, " ");
        }
        else {
                process = param;
        }


        pid_t pid = strtoul(process, nullptr, 10);
        if (pid < 1) {
                return;
        }

        if (sig == nullptr) {
                kill(pid, 15);
        }
        else if (strcmp(sig, "TERM") == 0) {
                kill(pid, 15);
        }
        else if (strcmp(sig, "KILL") == 0) {
                kill(pid, 9);
        }
        else {
                printf("This is not a valid signal. I won't kill the process.\n");
        }
}
