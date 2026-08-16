#include "libc.h"
#include "tty.h"
#include "drivers/gpio.h"
#include "drivers/sd_card.h"
#include "drivers/time.h"
#include "fs/file.h"
#include "fs/mbr.h"
#include "fs/ramfs.h"
#include "kernel/error.h"
#include "kernel/memory.h"
#include "kernel/network.h"
#include "kernel/proc.h"
#include "kernel/resets.h"
#include "kernel/time.h"
#include "userspaceVi/visual_editor.h"

// DO NOT TRY TO CALL KERNEL FUNCTIONS FROM USER SPACE OTHER THAN SYSCALLS!!!


struct cpio_newc_header {
        char c_magic[6];
        char c_ino[8];
        char c_mode[8];
        char c_uid[8];
        char c_gid[8];
        char c_nlink[8];
        char c_mtime[8];
        char c_filesize[8];
        char c_devmajor[8];
        char c_devminor[8];
        char c_rdevmajor[8];
        char c_rdevminor[8];
        char c_namesize[8];
        char c_check[8];
};

constexpr int cpio_header_len = sizeof(struct cpio_newc_header);


int load_initramfs(void) {
        const int fd = open("/mnt/disk0/initrfs.cio", O_RDONLY, 0);
        if (fd < 0) {
                return ENOENT;
        }

        // This is not optimal solution. The one would be to load multiples of 512 chunks
        // and parse the bytes from the array. The problem is at the end of such buffer,
        // as when the header is split and not read fully, one would have to read half the
        // header, load the other half and write to it.
        // To avoid this additional logic, we will rely on the logic of the FS.

        while (1) {
                char header_buffer[cpio_header_len];
                if (read(fd, header_buffer, cpio_header_len) < cpio_header_len) {
                        dprintf(2, "CPIO header broken. Could not read header.");
                }

                const struct cpio_newc_header *header = (struct cpio_newc_header *) header_buffer;

                if (memcmp(header->c_magic, "070701", 6) != 0) {
                        printf("Error parsing cpio header.\n");
                        break;
                }

                const off_t current_offset = lseek(fd, 0, SEEK_CUR);
                char next_bytes[10];
                read(fd, next_bytes, 10);
                if (memcmp(next_bytes, "TRAILER!!!", 10) == 0) {
                        printf("\x1b[96;49m[!] Unpacking ended successfully.\x1b[0m\n");
                        break;
                }
                lseek(fd, current_offset, SEEK_SET);


                char buf[128] = {};
                buf[8] = 0;

                memcpy(buf, header->c_mode, 8);
                const auto c_mode = strtoul(buf, nullptr, 16);
                memcpy(buf, header->c_namesize, 8);
                const auto c_namesize = strtoul(buf, nullptr, 16);
                memcpy(buf, header->c_filesize, 8);
                const auto c_filesize = strtoul(buf, nullptr, 16);

                if (c_namesize > 128) {
                        printf("Path too long, currently unsupported.\n");
                        break;
                }
                buf[127] = 0;
                read(fd, buf, c_namesize);


                if ((c_mode & 0xf000) == 0x4000) {
                        const int dirfd = open(buf, O_DIRECTORY | O_CREAT, 0);
                        close(dirfd);

                        const off_t offset = lseek(fd, 0, SEEK_CUR);
                        const int padding = 4 - (offset % 4);
                        lseek(fd, padding % 4, SEEK_CUR);
                }
                else {
                        const int created_fd = open(buf, O_CREAT, 0);

                        off_t offset = lseek(fd, 0, SEEK_CUR);
                        int padding = 4 - (offset % 4);
                        lseek(fd, padding % 4, SEEK_CUR);

                        // char file_buffer[c_filesize]; //malloc or it will go VERY bad, as e.g. gsh is 8060 bytes!!!
                        char *file_buffer = kmalloc(sizeof(char) * c_filesize);
                        read(fd, file_buffer, c_filesize);
                        write(created_fd, file_buffer, c_filesize);
                        kfree(file_buffer);

                        close(created_fd);

                        offset = lseek(fd, 0, SEEK_CUR);
                        padding = 4 - (offset % 4);
                        lseek(fd, padding % 4, SEEK_CUR);
                }
        }
}

void proc1_terminate_signal_handler(int signum) {
        if (signum == SIGTERM) {
                printf("[SIGTERM DETECTED] I don't want to exit, but as you wish.\n");
        }

        exit(-1);
}

void proc1(void) {
        signal(SIGTERM, proc1_terminate_signal_handler);
        setpgid(0, 1);

        while (1) {
                xor_pin(11);
                delay_ms(250);
        }
}

void PATER_ADAMVS_SIGINT(int signum) {
        printf("\x1b[91;49mTrying to exit the init process is a bloody bad idea.\x1b[0m\n");
}

void create_long_file(void) {
        int fd = open("/mnt/disk0/atst.txt", O_CREAT | O_WRONLY, 0);
        if (fd < 0) {
                return;
        }

        char *buf = malloc(2048);
        memset(buf, 0, 2048);
        off_t offset = 0;
        for (int i = 1; i < 400; ++i) {
                snprintf(buf + offset, 2048 - offset, "%i\n", i);

                int j = i;
                while (j) {
                        offset += 1;
                        j /= 10;
                }
                offset += 1; // \n
        }

        write(fd, buf, strlen(buf));
        free(buf);
}

#define NTP_UNIX_EPOCH_DIFF 2208988800UL

void set_current_time(void) {
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
        inet_aton("192.168.1.50", &dest.sin_addr);

        if (connect(sockfd, (struct sockaddr *) &dest, sizeof(dest)) < 0) {
                dprintf(2, "Error while trying to connect - connection timed out\n");
                close(sockfd);
                stime(&unix_time);
                return;
        }

        uint8_t buffer[56] = {0x23};
        write(sockfd, buffer, 48);
        const int received_bytes = read(sockfd, buffer, sizeof(buffer));

        time_t timestamp = 0;
        for (int i = 0; i < 4; ++i) {
                timestamp = (timestamp << 8) | buffer[48 + i];
        }

        unix_time = timestamp - NTP_UNIX_EPOCH_DIFF;
        stime(&unix_time);

        close(sockfd);
}

void PATER_ADAMVS(int argc, char *argv[]) {
        signal(SIGINT, PATER_ADAMVS_SIGINT);
        printf(
                "\n\x1b[96;49m  PATER ADAMVS QUI EST IN PARADISO VOLVPTATIS SALVTAT SEQUENTES PROCESS FILIOS\x1b[0m\n\n");


        printf("\x1b[96;49m[!] Running process LED\x1b[0m\n");
        [[maybe_unused]] const int proc1_pid = spawnp(proc1, nullptr, nullptr, nullptr, nullptr);

        printf("\x1b[96;49m[!] Unpacking initramfs\x1b[0m\n");
        load_initramfs();

        printf("\x1b[96;49m[!] Mounting initramfs\x1b[0m\n");
        const int cd_code = chdir("initramfs");
        if (cd_code == -1) {
                printf("\x1b[91;49m[!] No such file.\x1b[0m\n");
                __asm__("bkpt   #0");
        }

        set_current_time();
        time_t current_time = time(nullptr);
        printf("Current time is: %i\n", current_time);

        create_long_file();

        char *params[] = {"vi", "/mnt/disk0/start.s"};
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

int main(void) {
        reset_subsys();
        setup_internal_clk();
        init_tty();
        printk("   --- \x1b[91;49mG\x1b[93;49me\x1b[92;49mT \x1b[94;49mO\x1b[95;49mS\x1b[0m Kernel startup ---\n\n");
        int res;

        init_pin_output(25);
        init_pin_output(11);

        printk_status_init("Initializing scheduler");
        void *msp;
        __asm__("mrs    %0, msp" : "=r"(msp));
        res = scheduler_init(msp);
        printk_status_finish(res);


        printk_status_init("Mounting ramfs");
        struct Dentry *root = ramfs_mount(nullptr, nullptr, nullptr, 0);
        if (!root) {
                kernel_panic("RAMFS could not be mounted.", __FILE__, __LINE__, __func__);
        }
        printk_status_step();
        constexpr size_t root_dirs_count = 2;
        const char *root_dirs[root_dirs_count] = {"dev", "mnt"};
        for (size_t i = 0; i < root_dirs_count; ++i) {
                struct Dentry file = {
                        .name = root_dirs[i],
                        .inode = root->inode->i_sb->s_op->alloc_inode(root->inode->i_sb),
                };

                root->inode->i_op->create(root->inode, &file, S_IFDIR | 0666);
        }
        printk_status_step();

        struct Dentry dev = {.name = "dev"};
        root->inode->i_op->lookup(root->inode, &dev, 0);
        struct Dentry tty_dentry = {
                .name = "tty0",
                .inode = root->inode->i_sb->s_op->alloc_inode(root->inode->i_sb),
        };
        dev.inode->i_op->create(dev.inode, &tty_dentry, S_IFCHR | 0666);
        struct Dentry *tty = dev.inode->i_op->lookup(dev.inode, &tty_dentry, 0);
        printk_status_step();

        res = setup_tty_chrfile(tty->inode);
        printk_status_finish(res);

        struct HardDriveOperations *sd_op = init_sd_card();
        struct Dentry mnt = {.name = "mnt"};
        root->inode->i_op->lookup(root->inode, &mnt, 0);
        if (sd_op && mnt.inode) {
                struct PartitionTableEntry *partition_table = get_mbr_partition_table(sd_op);

                for (int i = 0; i < 4; ++i) {
                        mount_partition(&mnt, partition_table[i].lba_start, sd_op);
                }

                kfree(partition_table);
        }

        init_network();

        printk_status_init("Creating process init");
        res = create_process_init((void (*)(void)) PATER_ADAMVS, root->inode);
        printk_status_finish(res);

        printk("\n   --- Switching to process init ---\n\n");
        run_process_init();
        return 0;
}
