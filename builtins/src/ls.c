//
// Created by Artur Twardzik on 17/08/2026.
//

#include "libc.h"

static constexpr size_t MAX_FILENAME_LEN = 32;

struct DirectoryEntry {
        uint8_t file_type;
        uint32_t inode;
        // uint16_t rec_len;
        char name[MAX_FILENAME_LEN];
};

int main(int argc, char *argv[]) {
        const int dirfd = open(argv[1], O_RDONLY | O_DIRECTORY, 0);
        if (dirfd < 0) {
                dprintf(2, "ls: %s: No such file or directory.", argv[1]);
                return 1;
        }

        struct DirectoryEntry *buf = malloc(1024);
        if (!buf) {
                dprintf(2, "ls: not enough memory.");
                return 1;
        }
        const int read_bytes = readdir(dirfd, buf, 1024);

        for (unsigned int i = 0; i < read_bytes / sizeof(struct DirectoryEntry); ++i) {
                printf("%s\n", buf[i].name);
        }

        free(buf);

        return 0;
}
