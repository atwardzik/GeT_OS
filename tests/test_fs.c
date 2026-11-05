//
// Created by Artur Twardzik on 05/11/2026.
//

#include "tests.h"
#include "fs/file.h"

void test_FAT16_create_file(void) {
        // When
        unlink("/mnt/disk0/crdel.txt");
        const int fd = open("/mnt/disk0/crdel.txt", O_CREAT | O_WRONLY, 0);
        if (fd < 0) {
                FAIL;
        }

        // Then
        const int dirfd = open("/mnt/disk0", O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                FAIL;
        }

        struct DirectoryEntry *buf = malloc(1024);
        const int read_bytes = readdir(dirfd, buf, 1024);

        bool found = false;
        for (unsigned int i = 0; i < read_bytes / sizeof(struct DirectoryEntry); ++i) {
                if (strcmp(buf[i].name, "CRDEL.TXT") == 0) {
                        found = true;
                }
        }

        ASSERT_EQ(found, true);

        free(buf);
}

void test_FAT16_delete_file(void) {
        // When
        unlink("/mnt/disk0/crdel.txt");

        // Then
        const int dirfd = open("/mnt/disk0", O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                FAIL;
        }

        struct DirectoryEntry *buf = malloc(1024);
        const int read_bytes = readdir(dirfd, buf, 1024);

        bool found = false;
        for (unsigned int i = 0; i < read_bytes / sizeof(struct DirectoryEntry); ++i) {
                if (strcmp(buf[i].name, "CRDEL.TXT") == 0) {
                        found = true;
                }
        }

        ASSERT_EQ(found, false);

        free(buf);
}

void test_FAT16_create_long_file(void) {
        // Given
        unlink("/mnt/disk0/lfile.txt");
        const int fd = open("/mnt/disk0/lfile.txt", O_CREAT | O_WRONLY, 0);
        if (fd < 0) {
                FAIL;
        }

        // When
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

        // Then
        const int file_size = lseek(fd, 0, SEEK_END);
        ASSERT_EQ(file_size, offset);

        close(fd);
}