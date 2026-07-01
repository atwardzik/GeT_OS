//
// Created by Artur Twardzik on 22/11/2025.
//
#include "libc.h"

int main(int argc, char *argv[]) {
        if (argc < 2) {
                dprintf(2, "Not enough parameters\n\tUsage: %s [-ne] [file]\n", argv[0]);

                return -1;
        }

        bool LINE_NUMBERING = false;
        bool NEWLINE_CHARS_PRINTING = false;
        bool RAW_BYTES = false;
        char c;
        while ((c = getopt(argc, argv, "ner")) != (char) -1) {
                switch (c) {
                        case 'n':
                                LINE_NUMBERING = true;
                                break;
                        case 'e':
                                NEWLINE_CHARS_PRINTING = true;
                                break;
                        case 'r':
                                RAW_BYTES = true;
                                break;
                        case '?':
                        default:
                                printf("-%c - unknown parameter\n", c);
                                break;
                }
        }

        const char *file = nullptr;
        file = argv[argc - 1]; //temporary solution until global variables

        if (!file) {
                dprintf(2, "No file specified\n");

                return -1;
        }

#ifdef DEBUG
        printf("Line numbering is %s\n", LINE_NUMBERING ? "on" : "off");
        printf("Printing newline chars is %s\n", NEWLINE_CHARS_PRINTING ? "on" : "off");
        printf("File is: %s\n", file);
#endif

        const int fd = open(file, O_RDONLY, 0);
        if (fd < 0) {
                dprintf(2, "No such file.\n");

                return -1;
        }

        const unsigned long file_len = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        char *buffer = malloc(file_len);
        read(fd, buffer, file_len);

        unsigned long bytes_read = 0;
        int i = 1;
        if (LINE_NUMBERING) {
                puts("    1  ");
                i += 1;
        }
        do {
                const char *buf = &buffer[bytes_read];
                bytes_read += 1;
                if (buf[0] == '\n' && NEWLINE_CHARS_PRINTING) {
                        write(1, "$", 1);
                }

                if (bytes_read && RAW_BYTES) {
                        const unsigned char value = (unsigned char) strtoul(buf, nullptr, 10);

                        char str[20] = {};
                        itoa(value, str, 10);
                        write(1, str, strlen(str));
                }
                else if (bytes_read) {
                        write(1, buf, 1);
                }

                if (buf[0] == '\n' && LINE_NUMBERING) {
                        // dummy solution until printf supports fixed positions
                        int j = 10000;
                        while (j > i) {
                                puts(" ");

                                j /= 10;
                        }

                        printf("%i  ", i);
                        i += 1;
                }
        } while (bytes_read < file_len);

        if (NEWLINE_CHARS_PRINTING) {
                puts("%");
        }

        close(fd);
        return 0;
}
