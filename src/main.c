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

// DO NOT TRY TO CALL KERNEL FUNCTIONS FROM USER SPACE OTHER THAN SYSCALLS!!!

struct FileLine {
        unsigned int line_number;
        off_t file_offset;
        char *line;
};

/**
 * Appends offsets of next lines to the array of positions, starting at given index
 *
 * @param offsets array of offsets in given buffer
 * @param count last index in the array of offsets
 * @param positions_size size of the array of offsets
 * @param data data buffer
 * @param data_count size of the data buffer
 *
 * @return number of lines found in the given buffer
 */
int append_line_offsets(
        int *offsets, const int count, const size_t positions_size, const char *data, const size_t data_count
) {
        int endl_position = -1;
        int current_position = 0;
        int i = count;

        while (i < positions_size) {
                endl_position = strcspn(data + current_position, "\n");
                offsets[i] = current_position;

                i += 1;
                if (current_position + endl_position == data_count) {
                        break;
                }
                current_position += endl_position + 1;
        }

        return i - count;
}

int vi(int argc, char **argv) {
        // if (argc < 2) {
        //         dprintf(2, "[!] Not enough parameters supplied.");
        //         return 1;
        // }

        const int fd = open("/mnt/disk0/index.htm", O_RDONLY, 0);
        if (fd < 0) {
                dprintf(2, "[!] No such file.");
                return 1;
        }
        const size_t file_len = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        struct FileLine *screen = malloc(40 * sizeof(struct FileLine));
        if (!screen) {
                dprintf(2, "[!] Not enough memory.");
                return 1;
        }
        memset(screen, 0, 40 * sizeof(struct FileLine));

        char *buf = malloc(1025);
        if (!buf) {
                dprintf(2, "[!] Not enough memory.");
                return 1;
        }
        int file_offset = 0;
        int bytes_read = 0;
        int line_offsets[40] = {};
        int line_count = 0;
        int screen_line_index = 0;
        bool middle_of_line = false;
        bool exit_after_ending_line = false;
        while ((bytes_read = read(fd, buf, 1024))) {
                buf[1024] = 0;
                int block_line_count = 0;
                block_line_count = append_line_offsets(line_offsets, line_count, 40, buf, bytes_read);

                int line_index = screen_line_index;

                if (middle_of_line) {
                        line_index -= 1;
                        const size_t to_copy = strcspn(buf, "\n") + 1;

                        const size_t line_len = strlen(screen[line_index].line);
                        char *rline = realloc(screen[line_index].line, line_len + to_copy);
                        if (!rline) {
                                dprintf(2, "[!] Not enough memory.");
                                return 1;
                        }
                        screen[line_index].line = rline;
                        memmove(screen[line_index].line + line_len, buf, to_copy);
                        screen[line_index].line[line_len + to_copy] = 0;

                        middle_of_line = false;
                        line_index += 2;
                        if (exit_after_ending_line) {
                                break;
                        }
                }

                //it might be a single liner!
                for (int i = line_index; i < line_count + block_line_count; ++i) {
                        const off_t block_offset = line_offsets[i];
                        const size_t to_copy = i < line_count + block_line_count - 1
                                                       ? line_offsets[i + 1] - line_offsets[i]
                                                       : strcspn(buf + block_offset, "\n");

                        char *line = malloc(to_copy + 1);
                        if (!line) {
                                dprintf(2, "[!] Not enough memory.");
                                return 1;
                        }
                        memmove(line, buf + block_offset, to_copy);
                        line[to_copy] = 0;
                        screen[screen_line_index] = (struct FileLine){
                                screen_line_index,
                                file_offset + block_offset,
                                line
                        };
                        screen_line_index += 1;

                        if (strcspn(buf + block_offset, "\n") == to_copy && file_offset < file_len) {
                                middle_of_line = true;
                                break;
                        }
                }


                line_count += block_line_count;
                if (line_count == 40) {
                        if (middle_of_line) {
                                exit_after_ending_line = true;
                        }
                        else {
                                break;
                        }
                }
                file_offset += bytes_read;
        }

printfile:
        free(buf);
        printf("The File:\n");
        for (int i = 0; i < line_count; ++i) {
                char line[81];
                const int len = strcspn(screen[i].line, "\n");
                const int to_copy = len > 75 ? 75 : len;
                memcpy(line, screen[i].line, len);
                line[to_copy] = 0;
                printf("%i| %s\n", i, line);
        }


        for (int i = 0; i < 40; ++i) {
                free(screen[i].line);
        }
        free(screen);
        return 0;
}

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
                        printf("\x1b[96;40m[!] Unpacking ended successfully.\x1b[0m\n");
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
        printf("\x1b[91;40mTrying to exit the init process is a bloody bad idea.\x1b[0m\n");
}


void PATER_ADAMVS(int argc, char *argv[]) {
        signal(SIGINT, PATER_ADAMVS_SIGINT);
        printf(
                "\n\x1b[96;40m  PATER ADAMVS QUI EST IN PARADISO VOLVPTATIS SALVTAT SEQUENTES PROCESS FILIOS\x1b[0m\n\n");


        printf("\x1b[96;40m[!] Running process LED\x1b[0m\n");
        [[maybe_unused]] const int proc1_pid = spawnp(proc1, nullptr, nullptr, nullptr, nullptr);

        printf("\x1b[96;40m[!] Unpacking initramfs\x1b[0m\n");
        load_initramfs();

        printf("\x1b[96;40m[!] Mounting initramfs\x1b[0m\n");
        const int cd_code = chdir("initramfs");
        if (cd_code == -1) {
                printf("\x1b[91;40m[!] No such file.\x1b[0m\n");
                __asm__("bkpt   #0");
        }

        [[maybe_unused]] const int vi_pid = spawnp((void (*)(void)) vi, nullptr, nullptr, nullptr, nullptr);

        while (1) {
                printf("\x1b[96;40m[!] Running shell (gsh)\x1b[0m\n");
                int fd = open("bin/gsh", O_BINARY, 0);
                if (fd < 0) {
                        printf("\x1b[91;40m[!] No shell found.\x1b[0m\n");
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
                printf("\n\x1b[96;40m[PATER ADAMVS]\x1b[0m Child process %i exited with code: %i\n",
                       returned_pid, code);
                close(fd);
        }
}

int main(void) {
        reset_subsys();
        setup_internal_clk();
        init_tty();
        printk("   --- \x1b[91;40mG\x1b[93;40me\x1b[92;40mT \x1b[94;40mO\x1b[95;40mS\x1b[0m Kernel startup ---\n\n");
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
