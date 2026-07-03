//
// Created by Artur Twardzik on 03/06/2026.
//

#include "fat16.h"

#include "errno.h"
#include "kernel/error.h"
#include "kernel/memory.h"
#include "kernel/proc.h"

//FIXME: THIS IS ALL HIGHLY UNTESTED FOR BLOCK SIZES OTHER THAN 512

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

struct FAT16_SuperBlock {
        struct SuperBlock sb;

        struct FAT16_BootRecord boot_record;

        uint16_t fat_first_sector;
        uint16_t data_region_start;

        uint16_t FAT_cached_sector;
        uint16_t FAT_sector_cache[256];
};

struct FAT16_Inode {
        struct VFS_Inode vfs_inode;

        uint16_t first_cluster;
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


static size_t align_block_size(size_t count, size_t bytes_per_sector) {
        if (count % bytes_per_sector == 0) {
                return count;
        }

        return count - (count % bytes_per_sector) + bytes_per_sector;
}

static ssize_t read_root_directory(struct File *file, void *buf, const size_t count, const off_t file_offset) {
        const struct FAT16_Inode *inode = (struct FAT16_Inode *) file->f_inode;
        const auto sb = (struct FAT16_SuperBlock *) file->f_inode->i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        const uint16_t bytes_per_sector = sb->boot_record.bytes_per_sector;
        const off_t cluster_offset = file_offset / bytes_per_sector;
        const off_t sector_offset = file_offset % bytes_per_sector;

        const size_t block_size = align_block_size(count, bytes_per_sector);

        char *temp_buf = kmalloc(block_size);
        if (sb_op->hd_op.read_block(inode->first_cluster + cluster_offset, block_size, temp_buf) == 0) {
                memcpy(buf, temp_buf + sector_offset, count);

                file->f_pos += count;
                kfree(temp_buf);
                return count;
        }

        kfree(temp_buf);
        return 0;
}

static uint16_t FAT_fetch_next_cluster(struct FAT16_SuperBlock *sb, const uint16_t current_cluster) {
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;
        const uint16_t current_cluster_FAT_sector = current_cluster / 256;
        const uint16_t current_cluster_position_in_FAT_sector = current_cluster % 256;

        const uint16_t FAT_cached_range_start = sb->FAT_cached_sector * 256;
        const uint16_t FAT_cached_range_end = (sb->FAT_cached_sector + 1) * 256;

        if (FAT_cached_range_start <= current_cluster && current_cluster < FAT_cached_range_end) {
                return sb->FAT_sector_cache[current_cluster_position_in_FAT_sector];
        }


        sb_op->hd_op.read_block(sb->fat_first_sector + current_cluster_FAT_sector, 512, (char *) sb->FAT_sector_cache);
        return sb->FAT_sector_cache[current_cluster_position_in_FAT_sector];
}

static ssize_t follow_fat_chain(struct File *file, void *buf, const size_t count, const off_t file_offset) {
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
                cluster = FAT_fetch_next_cluster(sb, cluster);
        }

        int i = 0;
        const int cluster_bytes_len = sb->boot_record.sectors_per_cluster * bytes_per_sector;
        char *temp_buf = kmalloc(cluster_bytes_len);
        if (!temp_buf) {
                return -ENOMEM;
        }

        size_t remaining_bytes = count;
        do {
                const uint16_t physical_sector = sb->data_region_start +
                                                 ((cluster - 2) * sb->boot_record.sectors_per_cluster);


                if (sb_op->hd_op.read_block(physical_sector, cluster_bytes_len, temp_buf) != 0) {
                        return -1; //could not read sectors
                }

                const size_t skip = (i == 0 && offset_in_cluster > 0) ? offset_in_cluster : 0;
                const size_t valid_data_len = cluster_bytes_len - skip;
                const size_t to_copy = remaining_bytes > valid_data_len ? valid_data_len : remaining_bytes;

                memcpy(buf + count - remaining_bytes, temp_buf + skip, to_copy);
                remaining_bytes -= to_copy;
                i += 1;

                cluster = FAT_fetch_next_cluster(sb, cluster);
                //fixme: what if file_offset + count > filesize?
        } while (remaining_bytes && cluster >= 3 && cluster <= 0xffef);

        kfree(temp_buf);
        const off_t starting_position = file->f_pos;
        if (file->f_pos + count > file->f_inode->i_size) {
                file->f_pos = file->f_inode->i_size;
        }
        else {
                file->f_pos += count;
        }
        return file->f_pos - starting_position;
}

static ssize_t FAT16_read(struct File *file, void *buf, const size_t count, const off_t file_offset) {
        const struct FAT16_Inode *inode = (struct FAT16_Inode *) file->f_inode;
        const auto sb = (struct FAT16_SuperBlock *) file->f_inode->i_sb;

        const uint16_t root_cluster = ((struct FAT16_Inode *) sb->sb.s_root->inode)->first_cluster;
        if (inode->first_cluster == root_cluster) {
                return read_root_directory(file, buf, count, file_offset);
        }

        return follow_fat_chain(file, buf, count, file_offset);
}

static struct FAT16_DirectoryEntry *FAT16_find_file_dentry(
        const char *direntries, const size_t direntries_size, struct Dentry *file
) {
        char name[10] = {};
        memcpy(name, file->name, 10);
        int extension_index = -1;
        for (int i = 0; i <= 8; ++i) {
                if (name[i] == '.') {
                        name[i] = 0;
                        extension_index = i;
                }
        }
        name[8] = 0; //just in case the file does not have a dot

        char extension[4] = {};
        if (extension_index > 0) {
                memcpy(extension, file->name + extension_index + 1, 3);
                for (int i = 0; i < 3; ++i) {
                        if (extension[i] == 0x20) {
                                extension[i] = 0;
                        }
                }
        }

        const unsigned int name_length = strlen(name);
        const unsigned int extension_length = strlen(extension);


        for (size_t i = 0; i < direntries_size / 32; ++i) {
                const auto dirent = (struct FAT16_DirectoryEntry *) (direntries + i * 32);

                if (strncasecmp(name, dirent->filename, name_length) == 0 &&
                    ((!extension[0] && dirent->extension[0] == 0x20) ||
                     strncasecmp(extension, dirent->extension, extension_length) == 0)
                ) {
                        return dirent;
                }
        }

        return nullptr;
}

static struct FAT16_DirectoryEntry *FAT16_find_file_dentry_with_first_cluster(
        const char *direntries, const size_t direntries_size, const uint16_t first_cluster
) {
        for (size_t i = 0; i < direntries_size / 32; ++i) {
                const auto dirent = (struct FAT16_DirectoryEntry *) (direntries + i * 32);

                if (dirent->first_cluster == first_cluster) {
                        return dirent;
                }
        }

        return nullptr;
}

static struct Dentry *FAT16_lookup_root_directory(struct FAT16_Inode *parent, struct Dentry *file, unsigned int) {
        struct File file_wrapper = {.f_inode = (struct VFS_Inode *) parent, .f_pos = 0};
        char *buf = kmalloc(sizeof(char) * 512);

        while (file_wrapper.f_pos < parent->vfs_inode.i_size - 1) {
                read_root_directory(&file_wrapper, buf, 512, file_wrapper.f_pos);

                struct FAT16_DirectoryEntry *dirent;
                if ((dirent = FAT16_find_file_dentry(buf, 512, file))) {
                        const auto found = (struct FAT16_Inode *) FAT16_alloc_inode(parent->vfs_inode.i_sb);
                        found->first_cluster = dirent->first_cluster;
                        found->vfs_inode.i_size = dirent->file_size;
                        found->vfs_inode.parent = (struct VFS_Inode *) parent;
                        //todo: creation, access and modification time


                        struct Process *current_process = scheduler_get_current_process();
                        add_to_owned_inodes(&current_process->owned_inodes, (struct VFS_Inode *) found);

                        file->inode = (struct VFS_Inode *) found;
                        kfree(buf);
                        return file;
                }
        }

        kfree(buf);
        return nullptr;
}

static struct Dentry *FAT16_lookup(struct VFS_Inode *parent, struct Dentry *file, unsigned int) {
        struct FAT16_Inode *inode = (struct FAT16_Inode *) parent;
        const auto sb = (struct FAT16_SuperBlock *) parent->i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        const uint16_t root_cluster = ((struct FAT16_Inode *) sb->sb.s_root->inode)->first_cluster;
        if (inode->first_cluster == root_cluster) {
                return FAT16_lookup_root_directory(inode, file, 0);
        }
        //todo: implement lookup for subdirectories
        return nullptr;
}


static ssize_t FAT16_write(struct File *file, void *buf, size_t count, off_t file_offset) {
        struct FAT16_Inode *inode = (struct FAT16_Inode *) file->f_inode;
        if (inode->vfs_inode.parent != inode->vfs_inode.i_sb->s_root->inode) {
                return -EINVAL;
        }

        auto rootdir = (struct FAT16_Inode *) inode->vfs_inode.parent;
        const auto sb = (struct FAT16_SuperBlock *) rootdir->vfs_inode.i_sb;
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;

        if (count - file_offset > sb->boot_record.bytes_per_sector * sb->boot_record.sectors_per_cluster) {
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

        //get root directory entries
        char *rootbuf = kmalloc(rootdir->vfs_inode.i_size);
        struct File file_wrapper = {.f_inode = inode->vfs_inode.parent, .f_pos = 0};
        if (read_root_directory(&file_wrapper, rootbuf, rootdir->vfs_inode.i_size, 0) != rootdir->vfs_inode.i_size) {
                kfree(rootbuf);
                return -1; //why would that happen?
        }

        //update root directory entries
        const auto dentry = FAT16_find_file_dentry_with_first_cluster(rootbuf, 512, inode->first_cluster);
        if (dentry) {
                dentry->file_size = to_write;
                sb_op->hd_op.write_block(rootdir->first_cluster, rootdir->vfs_inode.i_size, rootbuf);
                inode->vfs_inode.i_size = to_write;
                file->f_pos += to_write;
        }
        kfree(rootbuf);


        return to_write;
}

static int FAT16_create_file(struct VFS_Inode *parent, struct Dentry *new_file, uint16_t mode) {
        //todo: full file support
        struct FAT16_Inode *inode = (struct FAT16_Inode *) new_file->inode;
        if (!inode) {
                return -EINVAL;
        }

        if (parent != inode->vfs_inode.i_sb->s_root->inode) { //todo: add subdirectories; currently only root
                return -EINVAL;
        }


        struct FAT16_Inode *rootdir = (struct FAT16_Inode *) parent;
        struct FAT16_SuperBlock *sb = (struct FAT16_SuperBlock *) rootdir->vfs_inode.i_sb;

        struct Dentry file = {.name = new_file->name};
        if (FAT16_lookup(parent, &file, 0)) {
                //todo: remove from owned inodes...
                return -EEXIST;
        }

        char *root_dir_entries = kmalloc(rootdir->vfs_inode.i_size);
        struct File file_wrapper = {.f_inode = parent, .f_pos = 0};
        if (read_root_directory(&file_wrapper, root_dir_entries, rootdir->vfs_inode.i_size, 0) != rootdir->vfs_inode.
            i_size) {
                kfree(root_dir_entries);
                return -1; //why would that happen?
        }

        int root_free_direntry = -1;
        for (int i = 0; i < sb->boot_record.max_root_dentries; ++i) {
                if (root_dir_entries[32 * i] == 0x00 || root_dir_entries[32 * i] == 0xe5) {
                        root_free_direntry = i;
                        break;
                }
        }

        if (root_free_direntry < 0) {
                kfree(root_dir_entries);
                return -ENOMEM;
        }


        //find free FAT, currently for one cluster...
        const auto sb_op = (struct FAT16_SuperBlockOperations *) sb->sb.s_op;
        sb_op->hd_op.read_block(sb->fat_first_sector, 512, (char *) sb->FAT_sector_cache);
        int allocated_cluster = -1;
        for (int i = 0; i < 256; ++i) {
                if (sb->FAT_sector_cache[i] == 0x0000) {
                        sb->FAT_sector_cache[i] = 0xffff;
                        // --- UNSAFE WRITE ---
                        sb_op->hd_op.write_block(sb->fat_first_sector, 512, (char *) sb->FAT_sector_cache);
                        allocated_cluster = i;
                        break;
                }
        }

        if (allocated_cluster < 0) {
                kfree(root_dir_entries);
                return -ENOMEM;
        }


        struct FAT16_DirectoryEntry dirent = {0};
        const char *dot = strchr(new_file->name, '.');
        const size_t filename_len = dot ? dot - new_file->name : 0;
        const size_t write_filename_len = filename_len > 8 ? 8 : filename_len;
        memcpy(dirent.filename, new_file->name, write_filename_len);
        size_t extension_len = dot ? strlen(dot + 1) : 0;
        memcpy(dirent.extension, dot + 1, extension_len);
        dirent.first_cluster = allocated_cluster;
        *(struct FAT16_DirectoryEntry *) &root_dir_entries[32 * root_free_direntry] = dirent;
        sb_op->hd_op.write_block(rootdir->first_cluster, rootdir->vfs_inode.i_size, root_dir_entries);
        // --- UNSAFE WRITE ---

        kfree(root_dir_entries);


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
        i_op->create = FAT16_create_file;

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
        root_inode->first_cluster = block_number
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


int FAT16_decode_entry_name(struct FAT16_DirectoryEntry *entry, char *buf) {
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
