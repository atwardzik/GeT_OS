//
// Created by Artur Twardzik on 31/10/2025.
//

#ifndef OS_RAMFS_H
#define OS_RAMFS_H

#include "file.h"

struct RAMFS_Inode {
        struct VFS_Inode vfs_inode;

        size_t bytes_allocated;
        void *file_begin;
};


struct Dentry *ramfs_mount(
        const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags
);

struct VFS_Inode *ramfs_alloc_inode(struct SuperBlock *sb);

void ramfs_destroy_inode(struct VFS_Inode *);

struct Dentry *ramfs_lookup(struct VFS_Inode *parent, struct Dentry *file, unsigned int);

int ramfs_create_file(struct VFS_Inode *parent, struct Dentry *new_file, uint16_t mode);

ssize_t ramfs_write(struct File *file, void *buf, size_t count, off_t file_offset);

ssize_t ramfs_read(struct File *file, void *buf, size_t count, off_t file_offset);

ssize_t ramfs_readdir(struct VFS_Inode *inode, struct File *file, void *buf, size_t count);

struct File *ramfs_get_file_handler(struct Dentry *file, unsigned int flags);


#endif //OS_RAMFS_H
