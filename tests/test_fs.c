//
// Created by Artur Twardzik on 05/11/2026.
//

#include "tests.h"
#include "fs/file.h"

void test_RAMFS_create_file(void) {
        // When
        const int fd = open("/crdel.txt", O_CREAT | O_WRONLY, 0);
        if (fd < 0) {
                FAIL;
                return;
        }

        // Then
        const int dirfd = open("/", O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                FAIL;
                close(fd);
                return;
        }

        struct DirectoryEntry *buf = malloc(1024);
        const int read_bytes = readdir(dirfd, buf, 1024);

        bool found = false;
        for (unsigned int i = 0; i < read_bytes / sizeof(struct DirectoryEntry); ++i) {
                if (strcmp(buf[i].name, "crdel.txt") == 0) {
                        found = true;
                }
        }

        ASSERT_EQ(found, true);

        free(buf);
        close(fd);
        close(dirfd);
}

void test_RAMFS_write_file(void) {
        // Given
        const int fd = open("/crdel.txt", O_RDWR, 0);
        if (fd < 0) {
                FAIL;
                return;
        }

        // When
        const char *str = "Hello World!";
        write(fd, str, strlen(str));

        // Then
        const int file_len = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        if (file_len == strlen(str)) {
                char contents[16] = {};
                read(fd, contents, file_len);
                ASSERT_EQ(strcmp(str, contents), 0);
        }
        else {
                FAIL;
        }

        close(fd);
}

void test_RAMFS_delete_file(void) {
        // When
        unlink("/crdel.txt");

        // Then
        const int dirfd = open("/", O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                FAIL;
                return;
        }

        struct DirectoryEntry *buf = malloc(1024);
        const int read_bytes = readdir(dirfd, buf, 1024);

        bool found = false;
        for (unsigned int i = 0; i < read_bytes / sizeof(struct DirectoryEntry); ++i) {
                if (strcmp(buf[i].name, "crdel.txt") == 0) {
                        found = true;
                }
        }

        ASSERT_EQ(found, false);

        free(buf);
        close(dirfd);
}

void test_FAT16_create_file(void) {
        // When
        unlink("/mnt/disk0/crdel.txt");
        const int fd = open("/mnt/disk0/crdel.txt", O_CREAT | O_WRONLY, 0);
        if (fd < 0) {
                FAIL;
                return;
        }

        // Then
        const int dirfd = open("/mnt/disk0", O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                FAIL;
                close(fd);
                return;
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
        close(fd);
        close(dirfd);
}

void test_FAT16_write_file(void) {
        const int fd = open("/mnt/disk0/crdel.txt", O_RDWR, 0);
        if (fd < 0) {
                FAIL;
                return;
        }

        const char *str = "AA\nBB\nCC";
        write(fd, str, strlen(str));

        const int file_len = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        if (file_len == strlen(str)) {
                char contents[16] = {};
                read(fd, contents, file_len);

                ASSERT_EQ(strcmp(contents, str), 0);
        }
        else {
                FAIL;
        }

        close(fd);
}

void test_FAT16_write_middle_file(void) {
        const int fd = open("/mnt/disk0/crdel.txt", O_RDWR, 0);
        if (fd < 0) {
                FAIL;
                return;
        }

        const char *expected_str = "AA\nDD\nCC";
        lseek(fd, 3, SEEK_SET);
        write(fd, "DD", 2);

        const int file_len = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        if (file_len == strlen(expected_str)) {
                char contents[16] = {};
                read(fd, contents, file_len);

                ASSERT_EQ(strcmp(contents, expected_str), 0);
        }
        else {
                FAIL;
        }

        close(fd);
}

void test_FAT16_delete_file(void) {
        // When
        unlink("/mnt/disk0/crdel.txt");

        // Then
        const int dirfd = open("/mnt/disk0", O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                FAIL;
                return;
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
        close(dirfd);
}

void test_FAT16_create_long_file(void) {
        // Given
        unlink("/mnt/disk0/lfile.txt");
        const int fd = open("/mnt/disk0/lfile.txt", O_CREAT | O_WRONLY, 0);
        if (fd < 0) {
                FAIL;
                return;
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
