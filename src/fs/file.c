//
// Created by Artur Twardzik on 27/08/2025.
//

#include "fs/file.h"

#include "errno.h"
#include "fat16.h"
#include "libc.h"
#include "tty.h"
#include "kernel/memory.h"
#include "kernel/proc.h"


static struct VFS_Inode *get_parent_directory(const char *name) {
        const struct Process *current_process = scheduler_get_current_process();
        struct VFS_Inode *inode = current_process->pwd;

        if (!name) {
                return inode;
        }
        if (name[0] == '/') {
                inode = current_process->root;
                name += 1;
        }
        if (strlen(name) == 0) {
                return inode;
        }


        char *path = kmalloc(strlen(name) + 1);
        strcpy(path, name);
        const char *token = strtok(path, "/");
        while (token) {
                if (strcmp(token, "..") == 0) {
                        inode = inode->parent;
                        token = strtok(nullptr, "/");
                        continue;
                }

                struct Dentry dentry = {
                        .name = token,
                };

                const struct Dentry *result = inode->i_op->lookup(inode, &dentry, 0);
                if (result != &dentry || !(result->inode->i_mode & S_IFDIR)) {
                        kfree(path);
                        return nullptr;
                }

                inode = result->inode;
                token = strtok(nullptr, "/");
        }
        kfree(path);


        return inode;
}

static struct VFS_Inode *get_file(struct VFS_Inode *parent, const char *name) {
        if (strcmp(name, "..") == 0 && !parent) {
                const struct Process *current_process = scheduler_get_current_process();
                return current_process->pwd->parent;
        }
        if (strcmp(name, "..") == 0 && parent) {
                return parent->parent;
        }

        if (!parent) {
                return nullptr;
        }

        if (strlen(name) == 0) {
                return parent;
        }

        struct Dentry dentry = {.name = name};
        if (parent->i_op->lookup(parent, &dentry, 0) == &dentry) {
                return dentry.inode;
        }

        return nullptr;
}

static struct VFS_Inode *create_file(struct VFS_Inode *parent, const char *name, uint32_t mode) {
        struct Dentry file = {
                .name = name,
                .inode = parent->i_sb->s_op->alloc_inode(parent->i_sb),
        };

        if (!file.inode) {
                return nullptr;
        }

        if (parent->i_op->create(parent, &file, mode) != 0) {
                kfree(file.inode);
                return nullptr;
        }

        return file.inode;
}

static int get_file_descriptor(struct VFS_Inode *inode, int flags) {
        struct Process *current_process = scheduler_get_current_process();
        if (current_process->files.count >= MAX_OPEN_FILE_DESCRIPTORS) {
                printk("[!] Too much files opened.\n");
                __asm__("bkpt   #0");
                return -1;
        }

        for (size_t i = 3; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) { //first three file descriptors are untouched (stdio)
                const struct File *file = current_process->files.fdtable[i];
                if (file && file->f_inode == inode) {
                        return i;
                }
        }

        struct File *found_file = kmalloc(sizeof(*found_file));
        found_file->f_op = inode->i_fop;
        found_file->f_inode = inode;
        found_file->f_pos = 0;
        found_file->f_owner = current_process->pid;
        found_file->f_flags = flags;

        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                if (current_process->files.fdtable[i] == nullptr) {
                        current_process->files.fdtable[i] = found_file;
                        current_process->files.count += 1;

                        return i;
                }
        }

        return -1;
}

static char *get_file_path(const char *name) {
        const char *last_file = strrchr(name, '/');
        if (!last_file) {
                return nullptr;
        }

        char *path = kmalloc(strlen(name) + 1);

        size_t i = 0;
        while (name + i != last_file) {
                path[i] = name[i];

                i += 1;
        }

        if (i == 0) {
                path[0] = '/';
                path[1] = 0;
                return path;
        }

        path[i] = 0;
        return path;
}

int sys_open(const char *name, int flags, int mode) {
        char *path = get_file_path(name);

        const char *filename;
        if (path) {
                filename = strrchr(name, '/') + 1;
        }
        else {
                filename = name;
        }

        struct VFS_Inode *parent = get_parent_directory(path);
        kfree(path);
        struct VFS_Inode *file = get_file(parent, filename);

        if (!file && flags & O_CREAT) {
                const uint16_t file_mode = (flags & O_DIRECTORY) ? S_IFDIR | 0666 : S_IFREG | 0777;
                file = create_file(parent, filename, file_mode);
        }

        //todo: move getting_fildes here...
        if (file && flags & O_DIRECTORY && !(file->i_mode & S_IFDIR)) {
                return -1;
        }
        if (file && !(flags & O_DIRECTORY) && file->i_mode & S_IFDIR) {
                return -1;
        }
        if (file) {
                const int fd = get_file_descriptor(file, flags);

                if (fd < 0) {
                        return fd;
                }

                if (flags & O_APPEND) {
                        sys_lseek(fd, 0, SEEK_END);
                }
                else if (flags & O_TRUNC) {
                        file->i_size = 0;
                }

                return fd;
        }

        return -1;
}


int sys_close(int file) {
        if (file >= 0 && file <= 2) {
                return -1;
        }
        struct Process *current_process = scheduler_get_current_process();

        struct File *current_file = current_process->files.fdtable[file];
        // FIXME: should it be like that?
        if (current_file->f_inode) { //is allocatable?
                kfree(current_file);
        }
        else {
                current_file->f_op->close(current_file);
        }
        current_process->files.fdtable[file] = nullptr;

        current_process->files.count -= 1;

        return 0;
}


int sys_read(int file, char *ptr, int len) {
        struct Process const *current_process = scheduler_get_current_process();

        if (file >= current_process->files.count || file < 0 || current_process->files.fdtable[file] == nullptr) {
                printk("[!] There is no such file descriptor\n");
                __asm__("bkpt   #0");
                return -EBADF;
        }

        struct File *current_file = current_process->files.fdtable[file];
        if (!current_file->f_op->read) {
                return -1;
        }


        ssize_t read_bytes = current_file->f_op->read(current_file, ptr, len, current_file->f_pos);
        if (read_bytes < 0) {
                return read_bytes;
        }

        if (current_file->f_pos + len > current_file->f_inode->i_size) {
                current_file->f_pos = current_file->f_inode->i_size;
        }
        else {
                current_file->f_pos += len;
        }

        return read_bytes;
}

int sys_write(const int file, char *ptr, const int len) {
        struct Process const *current_process = scheduler_get_current_process();

        if (file >= current_process->files.count || file < 0 || current_process->files.fdtable[file] == nullptr) {
                printk("[!] There is no such file descriptor\n");
                __asm__("bkpt   #0");
                return -EBADF;
        }

        struct File *current_file = current_process->files.fdtable[file];
        if (!current_file->f_op->write) {
                return -1;
        }

        size_t written_bytes = current_file->f_op->write(current_file, ptr, len, current_file->f_pos);
        current_file->f_pos += written_bytes;

        return written_bytes;
}

int sys_ioctl(int file, unsigned long request, void *arg) {
        struct Process const *current_process = scheduler_get_current_process();

        if (file >= current_process->files.count || file < 0 || current_process->files.fdtable[file] == nullptr) {
                printk("[!] There is no such file descriptor\n");
                __asm__("bkpt   #0");
                return -1;
        }

        struct File *current_file = current_process->files.fdtable[file];
        if (current_file->f_op->ioctl) {
                return current_file->f_op->ioctl(current_file, request, arg);
        }

        return -1;
}

int sys_lseek(const int file, off_t offset, int whence) {
        struct Process const *current_process = scheduler_get_current_process();

        if (file >= current_process->files.count || file < 0 || current_process->files.fdtable[file] == nullptr) {
                printk("[!] There is no such file descriptor\n");
                __asm__("bkpt   #0");
                return -1;
        }

        struct File *current_file = current_process->files.fdtable[file];
        if (whence == SEEK_SET) {
                current_file->f_pos = offset;
        }
        else if (whence == SEEK_CUR) {
                current_file->f_pos += offset;
        }
        else if (whence == SEEK_END) {
                current_file->f_pos = current_file->f_inode->i_size + offset;
        }
        // if (current_file->f_op->lseek) {
        //         return current_file->f_op->lseek(current_file, offset, whence);
        // }

        return current_file->f_pos;
}

int sys_readdir(int dirfd, void *buf, int size) {
        const struct Process *current_process = scheduler_get_current_process();

        if (dirfd >= current_process->files.count || dirfd < 0 || current_process->files.fdtable[dirfd] == nullptr) {
                printk("[!] There is no such file descriptor\n");
                return -1;
        }

        struct File *parent_handler = current_process->files.fdtable[dirfd];

        if (parent_handler->f_op->readdir) {
                struct VFS_Inode *parent_inode = parent_handler->f_inode;
                const int bytes_read = parent_handler->f_op->readdir(parent_inode, parent_handler, buf, size);

                parent_handler->f_pos += bytes_read;
                return bytes_read;
        }

        return 0;
}

int sys_chdir(const char *path) {
        struct VFS_Inode *parent = get_parent_directory(path);

        if (parent) {
                struct Process *current_process = scheduler_get_current_process();
                current_process->pwd = parent;
                return 0;
        }

        return -1;
}

int sys_fstat(int fd, struct stat *st) {
        struct Process *current_process = scheduler_get_current_process();

        if (fd >= current_process->files.count || fd < 0 || current_process->files.fdtable[fd] == nullptr) {
                printk("[!] There is no such file descriptor\n");
                return -1;
        }

        const struct VFS_Inode *inode = current_process->files.fdtable[fd]->f_inode;

        st->st_mode = inode->i_mode;
        st->st_uid = inode->i_uid;
        st->st_gid = inode->i_gid;
        st->st_mtim.tv_sec = inode->i_mtime;
        st->st_size = inode->i_size;

        return 0;
}

char *sys_getcwd(char *buf, unsigned int len) {
        const struct Process *current_process = scheduler_get_current_process();
        struct VFS_Inode *inode = current_process->pwd;
        struct VFS_Inode *parent = inode->parent;

        buf[len - 1] = 0;
        buf[len - 2] = '/';
        size_t i = len - 2; // last byte written
        while (inode != current_process->root) {
                struct Dentry dentry = {
                        .inode = inode,
                };

                //fixme: this lookup depends on inode and not on name of the directory, what breaks under FAT16
                const struct Dentry *res = parent->i_op->lookup(parent, &dentry, 0);
                if (res != &dentry || strlen(res->name) > i) {
                        return nullptr;
                }
                i -= strlen(res->name);
                memcpy(buf + i, res->name, strlen(res->name));


                i -= 1;
                buf[i] = '/';

                inode = parent;
                parent = inode->parent;
        }

        return buf + i;
}


struct Dentry *mount_partition(
        struct Dentry *parent_dir, const uint32_t block_number, const struct HardDriveOperations *hd_op
) {
        // brute force for filesystem type
        struct Dentry *partition_root = nullptr;
        if ((partition_root = FAT16_mount(parent_dir, block_number, hd_op)) != nullptr) {
                return partition_root;
        }

        return partition_root;
}
