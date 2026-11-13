//
// Created by Artur Twardzik on 05/11/2026.
//
#include "init.h"

#include "libc.h"
#include "pin_config.h"
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

        while (1) {
                xor_pin(LED1);
                delay_ms(250);
        }
}

static void PATER_ADAMVS_SIGINT(int signum) {
        printf("\x1b[91;49mTrying to exit the init process is a bloody bad idea.\x1b[0m\n");
}

void PATER_ADAMVS(int argc, char *argv[]) {
#ifdef TESTS
        run_userspace_tests();
#endif
        signal(SIGINT, PATER_ADAMVS_SIGINT);
        printf(
                "\n\x1b[96;49m PATER ADAMVS QUI EST IN PARADISO VOLVPTATIS SALVTAT SEQUENTES PROCESS FILIOS \x1b[0m\n\n");


        set_current_time();

        printf("\x1b[96;49m[!] Running process LED\x1b[0m\n");
        char *const proc1_argv[2] = {"LED", nullptr};
        [[maybe_unused]] const int proc1_pid = spawnp(proc1, nullptr, nullptr, proc1_argv, nullptr);


        load_initramfs();

        printf("\x1b[96;49m[!] Mounting initramfs\x1b[0m\n");
        const int cd_code = chdir("initramfs");
        if (cd_code == -1) {
                printf("\x1b[91;49m[!] No such file.\x1b[0m\n");
                __asm__("bkpt   #0");
        }


        while (1) {
                printf("\x1b[96;49m[!] Running shell (gsh)\x1b[0m\n");
                int fd = open("/mnt/disk0/gsh", O_BINARY, 0);
                if (fd < 0) {
                        printf("\x1b[91;49m[!] No shell found.\x1b[0m\n");
                        __asm__("bkpt   #0");
                }

                char *const program_args[] = {"gsh", nullptr};
                [[maybe_unused]] pid_t shell_pid = spawn(fd, nullptr, nullptr, program_args, nullptr);
                setpgid(shell_pid, 1);

                int code;
                const int returned_pid = waitpid(shell_pid, &code, 0);

                printf("\n\x1b[96;49m[PATER ADAMVS]\x1b[0m Child process %i exited with code: %i\n",
                       returned_pid, code);
                close(fd);
        }
}
