//
// Created by Artur Twardzik on 01/11/2025.
//

#include "ramfs.h"

#include "libc.h"
#include "kernel/memory.h"

struct Dentry *ramfs_mount(
        const char *source, const char *target_dir, const char *filesystemtype, unsigned long mountflags
) {
        struct SuperBlockOperations *sb_op = kmalloc(sizeof(*sb_op));
        sb_op->alloc_inode = &ramfs_alloc_inode;
        sb_op->destroy_inode = &ramfs_destroy_inode;

        struct SuperBlock *sb = kmalloc(sizeof(*sb));
        sb->name = "ramfs";
        sb->s_op = sb_op;

        struct VFS_Inode *root_inode = sb->s_op->alloc_inode(sb);
        root_inode->i_mode |= S_IFDIR;
        struct Dentry *root = kmalloc(sizeof(*root));
        root->name = "/";
        root->inode = root_inode;
        root->sb = sb;

        sb->s_root = root;

        return root;
}

struct VFS_Inode *ramfs_alloc_inode(struct SuperBlock *sb) {
        struct InodeOperations *i_op = kmalloc(sizeof(*i_op));
        i_op->lookup = &ramfs_lookup;
        i_op->create = &ramfs_create_file;

        struct FileOperations *i_fop = kmalloc(sizeof(*i_fop));
        i_fop->read = ramfs_read;
        i_fop->write = ramfs_write;


        struct RAMFS_Inode *inode = kmalloc(sizeof(*inode));
        memset(inode, 0, sizeof(*inode));
        inode->vfs_inode.i_op = i_op;
        inode->vfs_inode.i_fop = i_fop;
        inode->vfs_inode.i_sb = sb;
        inode->file_begin = nullptr;

        return (struct VFS_Inode *) inode;
}

void ramfs_destroy_inode(struct VFS_Inode *) {
        //todo: mark inode_table[destroyed_inode] as nullptr
        //todo: or if the directory contains direct pointers to the inodes invalidate directory entries
}


struct Dentry *ramfs_lookup(struct VFS_Inode *parent, struct Dentry *file, unsigned int) {
        char *buf = kmalloc(sizeof(char) * parent->i_size);
        struct File parent_wrapper = {
                .f_inode = parent
        };
        parent->i_fop->read(&parent_wrapper, buf, parent->i_size, 0);

        size_t offset = 0;
        while (offset < parent->i_size) {
                const void *next_offset = buf + offset;
                const struct DirectoryEntry *file_dentry = next_offset;
                struct VFS_Inode *file_inode = (struct VFS_Inode *) file_dentry->inode;

                if (strcmp(file_dentry->name, file->name) == 0 || file_inode == file->inode) {
                        file->name = file_dentry->name;
                        file->inode = file_inode;
                        file->sb = file->inode->i_sb;

                        kfree(buf);
                        return file;
                }

                offset += sizeof(struct DirectoryEntry);
        }

        kfree(buf);
        return nullptr;
}


int ramfs_create_file(struct VFS_Inode *parent, struct Dentry *new_file, uint16_t mode) {
        struct SuperBlock *sb = parent->i_sb;

        struct VFS_Inode *inode = new_file->inode;
        if (!inode) {
                return -1;
        }
        inode->i_mode = mode;
        inode->parent = parent;

        //update parent contents

        struct DirectoryEntry *directory_entry = kmalloc(sizeof(*directory_entry));
        directory_entry->inode = (uint32_t) (uintptr_t) inode;
        if (mode & S_IFREG) {
                directory_entry->file_type = '-';
        }
        else if (mode & S_IFDIR) {
                directory_entry->file_type = 'd';
        }
        else if (mode & S_IFCHR) {
                directory_entry->file_type = 'c';
        }
        else {
                // TODO: add other file types b(lock) s(ocket) p(ipe) l(ink)
        }

        const size_t name_len = strlen(new_file->name) > MAX_FILENAME_LEN - 1
                                        ? MAX_FILENAME_LEN - 1
                                        : strlen(new_file->name) + 1;
        memcpy(directory_entry->name, new_file->name, name_len);
        directory_entry->name[MAX_FILENAME_LEN - 1] = 0;

        struct File parent_handler = {
                .f_inode = parent,
        };
        parent->i_fop->write(&parent_handler, directory_entry, sizeof(struct DirectoryEntry), parent->i_size);

        kfree(directory_entry);
        return 0;
}

ssize_t ramfs_write(struct File *file, void *buf, size_t count, off_t file_offset) {
        struct RAMFS_Inode *inode_ptr = (struct RAMFS_Inode *) file->f_inode;

        if (!inode_ptr->bytes_allocated) {
                inode_ptr->file_begin = kmalloc(count + 512);
                inode_ptr->bytes_allocated = count + 512;
        }

        if (file_offset + count > inode_ptr->bytes_allocated) {
                inode_ptr->file_begin = krealloc(inode_ptr->file_begin, file_offset + count + 512);
                inode_ptr->bytes_allocated = file_offset + count + 512;
        }

        for (int i = 0; i < count; i++) {
                *(char *) (inode_ptr->file_begin + file_offset + i) = *(char *) (buf++);
        }

        inode_ptr->vfs_inode.i_size += count;

        return count;
}

ssize_t ramfs_read(struct File *file, void *buf, size_t count, off_t file_offset) {
        struct RAMFS_Inode *inode_ptr = (struct RAMFS_Inode *) file->f_inode;
        void *ptr = inode_ptr->file_begin;

        int offset = 0;
        while (offset < count
               && file_offset + offset < inode_ptr->vfs_inode.i_size
               && file_offset + offset < inode_ptr->bytes_allocated) {
                *(char *) (buf + offset) = *(char *) (ptr + file_offset + offset);

                offset += 1;
        }

        return offset;
}

struct File *ramfs_get_file_handler(struct Dentry *file, unsigned int flags) {
        struct FileOperations *std_fop = kmalloc(sizeof(*std_fop));
        memset(std_fop, 0, sizeof(*std_fop));
        std_fop->read = &ramfs_read;
        std_fop->write = &ramfs_write;

        struct File *f_std = kmalloc(sizeof(*f_std));
        f_std->f_pos = 0;
        f_std->f_op = std_fop;
        f_std->f_owner = 0;
        f_std->f_inode = file->inode;

        return f_std;
}
