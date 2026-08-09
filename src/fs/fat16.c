//
// Created by Artur Twardzik on 03/06/2026.
//

#include "fat16.h"

#include "errno.h"
#include "kernel/error.h"
#include "kernel/memory.h"
#include "kernel/proc.h"

//FIXME: THIS IS ALL HIGHLY UNTESTED FOR BLOCK SIZES OTHER THAN 512

// Sub-directories are created by allocating a cluster which then are cleared
// so it doesn't contain any directory entries. Two default entries are then
// created: The '.' entry point to the directory itself, and the '..' entry
// points to the parent directory. If the contents of a sub-directory grows
// beyond what can be in the cluster a new cluster is allocated in the same way
// as for files.

struct FAT16_BootRecord {
        uint8_t jump_code[3];
        const char oem_name[8];
        uint16_t bytes_per_sector;
        uint8_t sectors_per_cluster;
        uint16_t reserved_sectors;
        uint8_t FAT_copies;
        uint16_t max_root_dentries;
        uint16_t small_part_sectors_count;
        uint8_t media_descriptor;
        uint16_t sectors_per_FAT;
        uint16_t sectors_per_track;
        uint16_t heads_count;
        uint32_t hidden_sectors;
        uint32_t large_part_sectors_count;
        uint16_t drive_number;
        uint8_t extended_boot_signature;
        uint32_t volume_serial_number;
        const char volume_label[11];
        const char fs_type[8];
        uint8_t bootstrap_code[448];
        uint8_t boot_sector_signature;
} __attribute__((packed));

struct FAT16_SuperBlockOperations {
        struct SuperBlockOperations s_op;

        struct HardDriveOperations hd_op;
};

static constexpr size_t FAT_SECTOR_CACHE_SIZE = 256;

struct FAT16_SuperBlock {
        struct SuperBlock sb;

        struct FAT16_BootRecord boot_record;

        uint16_t fat_first_sector;
        uint16_t data_region_start;

        uint16_t FAT_cached_sector;
        uint16_t FAT_sector_cache[FAT_SECTOR_CACHE_SIZE];
};

struct FAT16_Inode {
        struct VFS_Inode vfs_inode;

        union {
                uint16_t first_cluster;
                uint16_t absolute_first_sector;
        };
};

struct FAT16_DirectoryEntries {
        struct FAT16_DirectoryEntry *dentries;
        unsigned int count;
};


static struct InodeOperations *i_op;
static struct FileOperations *i_fop;


static struct VFS_Inode *FAT16_alloc_inode(struct SuperBlock *sb) {
        struct FAT16_Inode *inode = kmalloc(sizeof(*inode));
        memset(inode, 0, sizeof(*inode));
        inode->vfs_inode.i_op = i_op;
        inode->vfs_inode.i_fop = i_fop;
        inode->vfs_inode.i_sb = sb;

        return (struct VFS_Inode *) inode;
}

static void FAT16_destroy_inode(struct VFS_Inode *) {}

static struct FAT16_DirectoryEntries FAT16_get_existing_rootdir_dentries(const struct FAT16_Inode *parent) {
        const auto sb = (struct FAT16_SuperBlock *) parent->vfs_inode.i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        const int bytes_per_cluster = sb->boot_record.bytes_per_sector * sb->boot_record.sectors_per_cluster;

        int offset = 0;
        int dentries_size = bytes_per_cluster;
        struct FAT16_DirectoryEntries dentries = {.dentries = kmalloc(dentries_size), .count = 0};
        if (!dentries.dentries) {
                return (struct FAT16_DirectoryEntries){.dentries = nullptr, .count = 0};
        }

        do {
                if (sb_op->hd_op.read_block(parent->absolute_first_sector + offset / sb->boot_record.bytes_per_sector,
                                            bytes_per_cluster,
                                            (void *) dentries.dentries + offset) != 0
                ) {
                        return dentries;
                }

                for (int i = dentries.count;
                     i < offset + bytes_per_cluster / sizeof(struct FAT16_DirectoryEntry);
                     ++i
                ) {
                        if (dentries.dentries[i].filename[0] == 0x00) {
                                return dentries;
                        }
                        dentries.count += 1;
                }

                offset += bytes_per_cluster;
                dentries_size += bytes_per_cluster;
                const auto new_dentries = krealloc(dentries.dentries, dentries_size);
                if (!new_dentries) {
                        return dentries;
                }
                dentries.dentries = new_dentries;
        } while (offset < parent->vfs_inode.i_size);

        return dentries;
}

static struct FAT16_DirectoryEntries FAT16_get_all_rootdir_dentries(const struct FAT16_Inode *parent) {
        const auto sb = (struct FAT16_SuperBlock *) parent->vfs_inode.i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        struct FAT16_DirectoryEntries dentries = {.dentries = kmalloc(parent->vfs_inode.i_size), .count = 0};
        if (!dentries.dentries) {
                return (struct FAT16_DirectoryEntries){.dentries = nullptr, .count = 0};
        }

        if (sb_op->hd_op.read_block(parent->absolute_first_sector,
                                    parent->vfs_inode.i_size,
                                    (void *) dentries.dentries) != 0
        ) {
                return dentries;
        }

        dentries.count = parent->vfs_inode.i_size / sizeof(struct FAT16_DirectoryEntry);
        return dentries;
}

static uint16_t FAT16_find_next_cluster(struct FAT16_SuperBlock *sb, const uint16_t current_cluster) {
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;
        const uint16_t current_cluster_FAT_sector = current_cluster / FAT_SECTOR_CACHE_SIZE;
        const uint16_t current_cluster_position_in_FAT_sector = current_cluster % FAT_SECTOR_CACHE_SIZE;

        const uint16_t FAT_cached_range_start = sb->FAT_cached_sector * FAT_SECTOR_CACHE_SIZE;
        const uint16_t FAT_cached_range_end = (sb->FAT_cached_sector + 1) * FAT_SECTOR_CACHE_SIZE;

        if (FAT_cached_range_start <= current_cluster && current_cluster < FAT_cached_range_end) {
                return sb->FAT_sector_cache[current_cluster_position_in_FAT_sector];
        }


        sb_op->hd_op.read_block(sb->fat_first_sector + current_cluster_FAT_sector,
                                sizeof(uint16_t) * FAT_SECTOR_CACHE_SIZE,
                                (char *) sb->FAT_sector_cache
        );
        sb->FAT_cached_sector = current_cluster_FAT_sector;
        return sb->FAT_sector_cache[current_cluster_position_in_FAT_sector];
}

static ssize_t FAT16_follow_fat_chain(struct File *file, void *buf, const size_t count, const off_t file_offset) {
        const struct FAT16_Inode *inode = (struct FAT16_Inode *) file->f_inode;
        if (file_offset >= inode->vfs_inode.i_size) {
                ((char *) buf)[0] = 0;
                return 0;
        }
        const auto sb = (struct FAT16_SuperBlock *) file->f_inode->i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        const uint16_t bytes_per_sector = sb->boot_record.bytes_per_sector;
        const uint16_t bytes_per_cluster = sb->boot_record.sectors_per_cluster * bytes_per_sector;

        const off_t cluster_start_offset = file_offset / bytes_per_cluster;
        const off_t offset_in_cluster = file_offset - (cluster_start_offset * bytes_per_cluster);


        uint16_t cluster = inode->first_cluster;
        //determine cluster for current file offset
        for (int i = 0; i < cluster_start_offset; ++i) {
                cluster = FAT16_find_next_cluster(sb, cluster);
        }

        int i = 0;
        char *temp_buf = kmalloc(bytes_per_cluster);
        if (!temp_buf) {
                return -ENOMEM;
        }

        const size_t bytes_to_read = file_offset + count > inode->vfs_inode.i_size
                                             ? inode->vfs_inode.i_size - file_offset
                                             : count;
        size_t remaining_bytes = bytes_to_read;
        do {
                const uint16_t physical_sector = sb->data_region_start +
                                                 ((cluster - 2) * sb->boot_record.sectors_per_cluster);


                if (sb_op->hd_op.read_block(physical_sector, bytes_per_cluster, temp_buf) != 0) {
                        return -1; //could not read sectors
                }

                const size_t skip = (i == 0 && offset_in_cluster > 0) ? offset_in_cluster : 0;
                const size_t valid_data_len = bytes_per_cluster - skip;
                const size_t to_copy = remaining_bytes > valid_data_len ? valid_data_len : remaining_bytes;

                memcpy(buf + count - remaining_bytes, temp_buf + skip, to_copy);
                remaining_bytes -= to_copy;
                i += 1;

                cluster = FAT16_find_next_cluster(sb, cluster);
                //fixme: what if file_offset + count > filesize?
        } while (remaining_bytes && cluster >= 3 && cluster <= 0xffef);

        kfree(temp_buf);
        return bytes_to_read - remaining_bytes;
}

static ssize_t FAT16_read(struct File *file, void *buf, const size_t count, const off_t file_offset) {
        const struct FAT16_Inode *inode = (struct FAT16_Inode *) file->f_inode;
        const auto sb = (struct FAT16_SuperBlock *) file->f_inode->i_sb;

        const uint16_t root_cluster = ((struct FAT16_Inode *) sb->sb.s_root->inode)->first_cluster;

        if (inode->first_cluster == root_cluster) {
                const struct FAT16_DirectoryEntries dentries = FAT16_get_existing_rootdir_dentries(inode);

                const size_t to_copy = file_offset + count > dentries.count * sizeof(struct FAT16_DirectoryEntry)
                                               ? 0
                                               : count;

                memcpy(buf, ((void *) dentries.dentries) + file_offset, to_copy);

                kfree(dentries.dentries);
                return to_copy;
        }

        return FAT16_follow_fat_chain(file, buf, count, file_offset);
}

static struct FAT16_DirectoryEntry *FAT16_find_file_dentry_with_first_cluster(
        const struct FAT16_DirectoryEntry *direntries, const uint16_t first_cluster
) {
        constexpr int max_dentries = 150'000; //roughly 4GB entries
        int i = 0;
        const struct FAT16_DirectoryEntry *dirent = &direntries[i * 32];
        while (dirent->filename[0] != 0x00 && i < max_dentries) {
                if (dirent->first_cluster == first_cluster) {
                        return dirent;
                }

                i += 1;
                dirent = &direntries[i];
        };

        return nullptr;
}

static struct Dentry *FAT16_lookup(struct VFS_Inode *parent, struct Dentry *file, unsigned int) {
        const struct FAT16_Inode *inode = (struct FAT16_Inode *) parent;

        struct FAT16_DirectoryEntries dentries = {0};
        if (parent == inode->vfs_inode.i_sb->s_root->inode) {
                dentries = FAT16_get_all_rootdir_dentries((struct FAT16_Inode *) parent);
        }
        else {
                void *entries = kmalloc(parent->i_size);
                if (!entries) {
                        return ERR_PTR(-ENOMEM);
                }

                struct File parent_wrapper = {.f_inode = parent};
                if (FAT16_read(&parent_wrapper, entries, parent->i_size, 0) != parent->i_size) {
                        return nullptr; //why would that happen?
                }

                dentries.dentries = entries;
                dentries.count = parent->i_size / sizeof(struct FAT16_DirectoryEntry);
        }
        if (dentries.count == 0) {
                kfree(dentries.dentries);
                return ERR_PTR(-ENOMEM);
        }

        const struct FAT16_DirectoryEntry *dentry = &dentries.dentries[0];
        int i = 0;
        while (dentry->filename[0] != 0x00) {
                char buf[16];
                FAT16_decode_entry_name(dentry, buf);
                if (strcasecmp(buf, file->name) == 0) {
                        const auto found = (struct FAT16_Inode *) FAT16_alloc_inode(inode->vfs_inode.i_sb);
                        found->first_cluster = dentry->first_cluster;
                        found->vfs_inode.i_size = dentry->file_size;
                        found->vfs_inode.parent = (struct VFS_Inode *) parent;
                        //todo: creation, access and modification time

                        struct Process *current_process = scheduler_get_current_process();
                        add_to_owned_inodes(&current_process->owned_inodes, (struct VFS_Inode *) found);

                        file->inode = (struct VFS_Inode *) found;
                        kfree(dentries.dentries);
                        return file;
                }

                i += 1;
                dentry = &dentries.dentries[i];
        }

        kfree(dentries.dentries);
        return nullptr;
}

static uint16_t FAT16_find_next_free_cluster(struct FAT16_SuperBlock *sb) {
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;
        const auto cluster_count = sb->boot_record.small_part_sectors_count / sb->boot_record.sectors_per_cluster;

        for (int i = 0; i < cluster_count / FAT_SECTOR_CACHE_SIZE; ++i) {
                sb_op->hd_op.read_block(sb->fat_first_sector,
                                        sizeof(uint16_t) * FAT_SECTOR_CACHE_SIZE,
                                        (char *) sb->FAT_sector_cache
                );
                sb->FAT_cached_sector = i;

                for (int j = 0; j < FAT_SECTOR_CACHE_SIZE; ++j) {
                        if (sb->FAT_sector_cache[j] == 0x0000) {
                                return i * FAT_SECTOR_CACHE_SIZE + j;
                        }
                }
        }

        return 0;
}

static int FAT16_mark_cluster(struct FAT16_SuperBlock *sb, const uint16_t cluster, const uint16_t value) {
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;
        const uint16_t current_cluster_FAT_sector = cluster / FAT_SECTOR_CACHE_SIZE;
        const uint16_t current_cluster_position_in_FAT_sector = cluster % FAT_SECTOR_CACHE_SIZE;

        const uint16_t FAT_cached_range_start = sb->FAT_cached_sector * FAT_SECTOR_CACHE_SIZE;
        const uint16_t FAT_cached_range_end = (sb->FAT_cached_sector + 1) * FAT_SECTOR_CACHE_SIZE;

        if (!(FAT_cached_range_start <= cluster && cluster < FAT_cached_range_end)) {
                sb_op->hd_op.read_block(sb->fat_first_sector + current_cluster_FAT_sector,
                                        sizeof(uint16_t) * FAT_SECTOR_CACHE_SIZE,
                                        (char *) sb->FAT_sector_cache
                );
                sb->FAT_cached_sector = current_cluster_FAT_sector;
        }

        sb->FAT_sector_cache[current_cluster_position_in_FAT_sector] = value;
        sb_op->hd_op.write_block(
                sb->fat_first_sector + current_cluster_FAT_sector,
                sizeof(uint16_t) * FAT_SECTOR_CACHE_SIZE,
                (char *) sb->FAT_sector_cache
        );

        return 0;
}

static int write_consecutive_sectors(
        struct FAT16_Inode *inode, uint32_t physical_sector, void *buf, const size_t count, const off_t offset
) {
        const auto sb = (struct FAT16_SuperBlock *) inode->vfs_inode.i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;
        const auto bytes_per_sector = sb->boot_record.bytes_per_sector;

        int total_written_bytes = 0;

        while (offset >= bytes_per_sector) {
                physical_sector += 1;
        }


        char *written_sector = kmalloc(bytes_per_sector);
        sb_op->hd_op.read_block(physical_sector, bytes_per_sector, written_sector);
        //if offset || offset + count < bytes_per_sector

        size_t to_write = offset + count > bytes_per_sector ? bytes_per_sector - offset : count;
        memcpy(written_sector + offset, buf, to_write);

        sb_op->hd_op.write_block(physical_sector, bytes_per_sector, buf);
        total_written_bytes = to_write;


        while (total_written_bytes < count) {
                physical_sector += 1;

                to_write = count - total_written_bytes < bytes_per_sector
                                   ? count - total_written_bytes
                                   : bytes_per_sector;
                if (to_write < bytes_per_sector) {
                        sb_op->hd_op.read_block(physical_sector, bytes_per_sector, written_sector);
                }
                memcpy(written_sector, buf + total_written_bytes, to_write);

                sb_op->hd_op.write_block(physical_sector, bytes_per_sector, buf + total_written_bytes);

                total_written_bytes += bytes_per_sector;
        }

        kfree(written_sector);
        return total_written_bytes > count ? count : total_written_bytes;
}

static ssize_t FAT16_write(struct File *file, void *buf, const size_t count, const off_t file_offset) {
        const auto inode = (struct FAT16_Inode *) file->f_inode;
        const auto parent = (struct FAT16_Inode *) inode->vfs_inode.parent;
        const auto rootdir = (struct FAT16_Inode *) inode->vfs_inode.i_sb->s_root->inode;

        if (inode == rootdir) {
                return write_consecutive_sectors(inode, inode->absolute_first_sector, buf, count, file_offset);
        }

        //fixme: temporary condition for writing only into root directory
        if (parent != rootdir) {
                return -EINVAL;
        }

        const auto sb = (struct FAT16_SuperBlock *) parent->vfs_inode.i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        struct FAT16_DirectoryEntries dentries = FAT16_get_existing_rootdir_dentries(parent);
        if (dentries.count == 0) {
                kfree(dentries.dentries);
                return -1; //why would that happen?
        }
        const auto dentry = FAT16_find_file_dentry_with_first_cluster(dentries.dentries, inode->first_cluster);
        if (!dentry) {
                kfree(dentries.dentries);
                return -1;
        }

        //resize file if need be
        if (file_offset + count >= dentry->file_size) {
                kfree(dentries.dentries);
                return -EINVAL;
        }

        //write file contents to the cluster
        char *current_file_block = kmalloc(512);
        const uint32_t physical_sector = sb->data_region_start + (inode->first_cluster - 2) * sb->boot_record.
                                         sectors_per_cluster;
        sb_op->hd_op.read_block(physical_sector, 512, current_file_block);
        const size_t available = 512 - file->f_pos;
        const size_t to_write = count > available ? available : count;
        memcpy(current_file_block + file->f_pos, buf, to_write);

        sb_op->hd_op.write_block(physical_sector, 512, buf); //fixme: unsafe boundaries, writing garbage
        kfree(current_file_block);

        //update root directory entries
        dentry->file_size = to_write;
        // sb_op->hd_op.write_block(parent->first_cluster, parent->vfs_inode.i_size, rootbuf);
        inode->vfs_inode.i_size = to_write;
        file->f_pos += to_write;
        //TODO: SAVE INTO ROOTDIR!
        kfree(dentries.dentries);


        return to_write;
}

static int find_free_dentry(const struct FAT16_DirectoryEntries *dentries) {
        for (int i = 0; i < dentries->count; ++i) {
                if (dentries->dentries[i].filename[0] == 0x00 || dentries->dentries[i].filename[0] == 0xe5) {
                        return i;
                }
        }

        return -1;
}

static int FAT16_create_file(struct FAT16_Inode *parent, struct Dentry *new_file, uint16_t mode) {
        //todo: full file support
        struct FAT16_Inode *inode = (struct FAT16_Inode *) new_file->inode;
        if (!inode) {
                return -EINVAL;
        }
        const auto sb = (struct FAT16_SuperBlock *) inode->vfs_inode.i_sb;

        struct Dentry file = {.name = new_file->name};
        if (FAT16_lookup((struct VFS_Inode *) parent, &file, 0)) {
                return -EEXIST;
        }

        struct FAT16_DirectoryEntries dentries = {0};
        if (parent == (struct FAT16_Inode *) inode->vfs_inode.i_sb->s_root->inode) {
                dentries = FAT16_get_all_rootdir_dentries(parent);
        }
        else {
                void *entries = kmalloc(parent->vfs_inode.i_size);
                if (!entries) {
                        return -ENOMEM;
                }

                struct File parent_wrapper = {.f_inode = (struct VFS_Inode *) parent};
                if (FAT16_read(&parent_wrapper, entries, parent->vfs_inode.i_size, 0) != parent->vfs_inode.i_size) {
                        return -1; //why would that happen?
                }

                dentries.dentries = entries;
                dentries.count = parent->vfs_inode.i_size / sizeof(struct FAT16_DirectoryEntry);
        }


        const int free_direntry = find_free_dentry(&dentries);
        if (free_direntry < 0) {
                //todo: after supporting subdirectories just add next dentry
                kfree(dentries.dentries);
                return -ENOMEM;
        }


        const uint16_t first_cluster = FAT16_find_next_free_cluster(sb);
        if (!first_cluster) {
                kfree(dentries.dentries);
                return -ENOMEM;
        }
        FAT16_mark_cluster(sb, first_cluster, 0xffff);


        struct FAT16_DirectoryEntry *dirent = &dentries.dentries[free_direntry];
        memset(dirent, 0, sizeof(struct FAT16_DirectoryEntry));
        FAT16_encode_entry_name(new_file->name, dirent);
        dirent->first_cluster = first_cluster;
        struct File parent_wrapper = {.f_inode = (struct VFS_Inode *) parent};
        FAT16_write(&parent_wrapper, dentries.dentries, dentries.count * sizeof(struct FAT16_DirectoryEntry), 0);

        kfree(dentries.dentries);

        return 0;
}

static struct FAT16_BootRecord read_boot_sector(const uint32_t block_number, const struct HardDriveOperations *hd_op) {
        char buf[512];
        hd_op->read_block(block_number, 512, buf);

        const struct FAT16_BootRecord boot_record = *(struct FAT16_BootRecord *) buf;

        return boot_record;
}

static unsigned int disk_num = 0;

struct Dentry *FAT16_mount(
        struct Dentry *parent_dir, const uint32_t block_number, const struct HardDriveOperations *hd_op
) {
        // static operations struct for future inode use
        i_op = kmalloc(sizeof(*i_op));
        i_op->lookup = FAT16_lookup;
        typedef int (*create_fn)(struct VFS_Inode *, struct Dentry *, uint16_t);
        i_op->create = (create_fn) FAT16_create_file;

        i_fop = kmalloc(sizeof(*i_fop));
        i_fop->read = FAT16_read;
        i_fop->write = FAT16_write;

        //filesystem structure
        struct FAT16_SuperBlockOperations *sb_op = kmalloc(sizeof(*sb_op));
        sb_op->s_op.alloc_inode = FAT16_alloc_inode;
        sb_op->s_op.destroy_inode = FAT16_destroy_inode;
        sb_op->hd_op = *hd_op;

        struct FAT16_SuperBlock *sb = kmalloc(sizeof(*sb));
        sb->sb.name = "fat16";
        sb->sb.s_op = (struct SuperBlockOperations *) sb_op;

        const auto boot_record = read_boot_sector(block_number, hd_op);
        memcpy(&sb->boot_record, &boot_record, sizeof(boot_record));
        sb->fat_first_sector = block_number + boot_record.reserved_sectors;
        sb_op->hd_op.read_block(sb->fat_first_sector, 512, (char *) sb->FAT_sector_cache);

        struct FAT16_Inode *root_inode = (struct FAT16_Inode *) FAT16_alloc_inode((struct SuperBlock *) sb);
        root_inode->vfs_inode.i_mode = S_IFDIR;
        root_inode->vfs_inode.parent = parent_dir->inode;
        root_inode->vfs_inode.i_fop = i_fop;
        root_inode->vfs_inode.i_op = i_op;
        root_inode->absolute_first_sector = block_number
                                            + boot_record.reserved_sectors
                                            + boot_record.FAT_copies * boot_record.sectors_per_FAT;
        root_inode->vfs_inode.i_size = boot_record.max_root_dentries * 32;

        sb->data_region_start = root_inode->first_cluster + (
                                        boot_record.max_root_dentries * 32 / boot_record.bytes_per_sector);

        struct Dentry *root = kmalloc(sizeof(*root));
        root->name = "/";
        root->inode = (struct VFS_Inode *) root_inode;
        root->sb = &sb->sb;

        sb->sb.s_root = root;


        //new Dentry in root
        char name[6] = "disk";
        char num[4] = {};
        strcat(name, itoa((int) disk_num, num, 10));
        disk_num += 1;

        struct Dentry parent_dir_entry = {.name = name, .inode = (struct VFS_Inode *) root_inode};
        parent_dir->inode->i_op->create(parent_dir->inode, &parent_dir_entry, S_IFDIR | 0755);


        return root;
}


int FAT16_decode_entry_name(const struct FAT16_DirectoryEntry *entry, char *buf) {
        char name[8] = {};
        memcpy(name, entry->filename, 8);
        char extension[3] = {};
        memcpy(extension, entry->extension, 3);

        int name_last_char = 0;
        for (int i = 0; i < 8; ++i) {
                if (name[i] == 0x20) {
                        break;
                }

                buf[i] = name[i];
                name_last_char += 1;
        }

        if (extension[0] != 0x20) {
                buf[name_last_char] = '.';
                name_last_char += 1;

                for (int i = 0; i < 3; ++i) {
                        if (extension[i] == 0x20) {
                                break;
                        }

                        buf[name_last_char] = extension[i];
                        name_last_char += 1;
                }
        }
        buf[name_last_char] = 0;


        return 0;
}

int FAT16_encode_entry_name(const char *name, struct FAT16_DirectoryEntry *entry) {
        memset(entry, 0x20, 11); // all names in FAT16 are 8+3, for shorter names remaining characters are 0x20

        const char *dot = strchr(name, '.');
        const size_t filename_len = dot ? dot - name : 0;
        const size_t write_filename_len = filename_len > 8 ? 8 : filename_len;
        memcpy(entry->filename, name, write_filename_len);

        const size_t extension_len = dot ? strlen(dot + 1) : 0;
        memcpy(entry->extension, dot + 1, extension_len);

        return 0;
}
