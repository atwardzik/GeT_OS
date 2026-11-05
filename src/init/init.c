//
// Created by Artur Twardzik on 05/11/2026.
//


#include "libc.h"
#include "drivers/gpio.h"
#include "drivers/time.h"

#ifdef TESTS
#include "tests.h"
#endif

static void proc1_terminate_signal_handler(int signum) {
        if (signum == SIGTERM) {
                printf("[SIGTERM DETECTED] I don't want to exit, but as you wish.\n");
        }

        exit(-1);
}

static void proc1(void) {
        signal(SIGTERM, proc1_terminate_signal_handler);
        setpgid(0, 1);

        while (1) {
                xor_pin(11);
                delay_ms(250);
        }
}

static void PATER_ADAMVS_SIGINT(int signum) {
        printf("\x1b[91;49mTrying to exit the init process is a bloody bad idea.\x1b[0m\n");
}

#define NTP_UNIX_EPOCH_DIFF 2208988800UL

static void set_current_time(void) {
        //TODO: save current time on shutdown and retrieve it from file if the internet connection is off
        time_t unix_time = 0;

        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
                dprintf(2, "Error while trying to open socket\n");
                stime(&unix_time);
                return;
        }
        struct sockaddr_in source = {AF_INET, htons(2512)};
        bind(sockfd, (struct sockaddr *) &source, sizeof(source));

        struct sockaddr_in dest = {AF_INET, htons(123)};
        inet_aton("192.168.1.57", &dest.sin_addr);
        // inet_aton("216.239.35.0", &dest.sin_addr); // time.google.com

        if (connect(sockfd, (struct sockaddr *) &dest, sizeof(dest)) < 0) {
                dprintf(2, "Error while trying to connect - connection timed out\n");
                close(sockfd);
                stime(&unix_time);
                return;
        }

        uint8_t buffer1[56];
        uint8_t buffer2[56];
retry:
        buffer1[0] = 0x23;
        buffer2[0] = 0x23;

        write(sockfd, buffer1, 48);
        read(sockfd, buffer1, sizeof(buffer1));
        uint32_t timestamp1 = 0;
        for (int i = 0; i < 4; ++i) {
                timestamp1 = (timestamp1 << 8) | buffer1[48 + i];
        }

        delay_ms(1);

        write(sockfd, buffer2, 48);
        read(sockfd, buffer2, sizeof(buffer2));
        uint32_t timestamp2 = 0;
        for (int i = 0; i < 4; ++i) {
                timestamp2 = (timestamp2 << 8) | buffer2[48 + i];
        }

        if (timestamp2 - timestamp1 > 1) { //TODO: add timeout
                goto retry;
        }

        unix_time = timestamp1 - NTP_UNIX_EPOCH_DIFF;
        stime(&unix_time);

        close(sockfd);
}

extern int load_initramfs(void);

void PATER_ADAMVS(int argc, char *argv[]) {
#ifdef TESTS
        run_userspace_tests();
#endif
        signal(SIGINT, PATER_ADAMVS_SIGINT);
        printf(
                "\n\x1b[96;49m PATER ADAMVS QUI EST IN PARADISO VOLVPTATIS SALVTAT SEQUENTES PROCESS FILIOS \x1b[0m\n\n");


        printf("\x1b[96;49m[!] Retrieving current time. \x1b[0m");
        set_current_time();
        time_t current_time = time(nullptr);
        printf("Current time is: %s\n", ctime(&current_time));


        printf("\x1b[96;49m[!] Running process LED\x1b[0m\n");
        char *const proc1_argv[2] = {"LED", nullptr};
        [[maybe_unused]] const int proc1_pid = spawnp(proc1, nullptr, nullptr, proc1_argv, nullptr);

        printf("\x1b[96;49m[!] Unpacking initramfs\x1b[0m\n");
        load_initramfs();

        printf("\x1b[96;49m[!] Mounting initramfs\x1b[0m\n");
        const int cd_code = chdir("initramfs");
        if (cd_code == -1) {
                printf("\x1b[91;49m[!] No such file.\x1b[0m\n");
                __asm__("bkpt   #0");
        }

        // char *params[] = {"vi", "/mnt/disk0/start.s", nullptr};
        // [[maybe_unused]] const int vi_pid = spawnp((void (*)(void)) run_editor, nullptr, nullptr, params, nullptr);

        // int i = 0;
        // int code;
        // const int returned_pid = wait(&code);
        while (1) {
                // i += 1;
                // if (returned_pid != vi_pid) {
                //         i = 0; //implement waitpid syscall
                // }
                // #if 0
                printf("\x1b[96;49m[!] Running shell (gsh)\x1b[0m\n");
                int fd = open("bin/gsh", O_BINARY, 0);
                if (fd < 0) {
                        printf("\x1b[91;49m[!] No shell found.\x1b[0m\n");
                        __asm__("bkpt   #0");
                }

                char *const program_args[] = {"gsh", nullptr};
                [[maybe_unused]] pid_t shell_pid = spawn(fd, nullptr, nullptr, program_args, nullptr);

        process_wait:
                int code;
                const int returned_pid = wait(&code);

                if (returned_pid != shell_pid) {
                        goto process_wait; //implement waitpid syscall
                }
                printf("\n\x1b[96;49m[PATER ADAMVS]\x1b[0m Child process %i exited with code: %i\n",
                       returned_pid, code);
                close(fd);
                // #endif
        }
}
