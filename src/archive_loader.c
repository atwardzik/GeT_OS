//
// Created by Artur Twardzik on 17/08/2026.
//

#include "errno.h"
#include "libc.h"
#include "fs/file.h"
#include "kernel/memory.h"

#if 0
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

int load_initramfs(struct VFS_Inode *root) {
        struct Dentry dentry = {.name = "initrfs.cio"};
        root->i_op->lookup(root, &dentry, 0);
        const int fd = sys_open("/mnt/disk0/initrfs.cio", O_RDONLY, 0);
        if (fd < 0) {
                return -ENOENT;
        }

        // This is not optimal solution. The one would be to load multiples of 512 chunks
        // and parse the bytes from the array. The problem is at the end of such buffer,
        // as when the header is split and not read fully, one would have to read half the
        // header, load the other half and write to it.
        // To avoid this additional logic, we will rely on the logic of the FS.

        while (1) {
                char header_buffer[cpio_header_len];
                if (sys_read(fd, header_buffer, cpio_header_len) < cpio_header_len) {
                        dprintf(2, "CPIO header broken. Could not read header.");
                }

                const struct cpio_newc_header *header = (struct cpio_newc_header *) header_buffer;

                if (memcmp(header->c_magic, "070701", 6) != 0) {
                        printf("Error parsing cpio header.\n");
                        break;
                }

                const off_t current_offset = sys_lseek(fd, 0, SEEK_CUR);
                char next_bytes[10];
                sys_read(fd, next_bytes, 10);
                if (memcmp(next_bytes, "TRAILER!!!", 10) == 0) {
                        printf("\x1b[96;49m[!] Unpacking ended successfully.\x1b[0m\n");
                        break;
                }
                sys_lseek(fd, current_offset, SEEK_SET);


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
                sys_read(fd, buf, c_namesize);


                if ((c_mode & 0xf000) == 0x4000) {
                        const int dirfd = sys_open(buf, O_DIRECTORY | O_CREAT, 0);
                        sys_close(dirfd);

                        const off_t offset = sys_lseek(fd, 0, SEEK_CUR);
                        const int padding = 4 - (offset % 4);
                        sys_lseek(fd, padding % 4, SEEK_CUR);
                }
                else {
                        const int created_fd = sys_open(buf, O_CREAT, 0);

                        off_t offset = sys_lseek(fd, 0, SEEK_CUR);
                        int padding = 4 - (offset % 4);
                        sys_lseek(fd, padding % 4, SEEK_CUR);

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
        return 0;
}
#endif
