//
// Created by Artur Twardzik on 03/06/2026.
//

#ifndef OS_FAT16_H
#define OS_FAT16_H

#include "file.h"

#include <stdint.h>

struct FAT16_DirectoryEntry {
        char filename[8];
        char extension[3];
        uint8_t attributes;
        uint8_t _reserved1;
        uint8_t c_milis;
        uint16_t c_time;
        uint16_t c_date;
        uint16_t a_time;
        uint16_t _reserved2;
        uint16_t m_time;
        uint16_t m_date;
        uint16_t first_cluster;
        uint32_t file_size;
} __attribute__((packed));

constexpr uint8_t FAT16_ATTRIB_READ_ONLY = 1;
constexpr uint8_t FAT16_ATTRIB_HIDDEN = 2;
constexpr uint8_t FAT16_ATTRIB_SYSTEM = 4;
constexpr uint8_t FAT16_ATTRIB_VOLUME_NAME = 8;
constexpr uint8_t FAT16_ATTRIB_DIRECTORY = 16;
constexpr uint8_t FAT16_ATTRIB_ACHIEVE = 32;

struct Dentry *FAT16_mount(
        struct Dentry *parent_dir, uint32_t block_number, const struct HardDriveOperations *hd_op
);

int FAT16_decode_entry_name(struct FAT16_DirectoryEntry *entry, char *buf);

int FAT16_encode_entry_name(const char *name, struct FAT16_DirectoryEntry *entry);


#endif //OS_FAT16_H
