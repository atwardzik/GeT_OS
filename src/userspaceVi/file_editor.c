//
// Created by Artur Twardzik on 08/07/2026.
//

#include "file_editor.h"

#include "errno.h"
#include "libc.h"
#include "types.h"

struct FileLine {
        off_t file_offset;
        uint16_t line_number; /* 65535 lines seems sufficient for most purposes */

        enum { FILE_OG, FILE_BAK, NONE } location;
} __attribute__((packed));

struct FileEditor {
        bool read_only;
        struct FileLine *lines; /* array index corresponds to line number */
        size_t lines_size;
        size_t lines_cap;

        const char *file_name;
        int fd;
        int fdbak;
};

static int index_file_lines(int fd, struct FileLine **lines, size_t *lines_size, size_t *lines_cap) {
        if (!lines_size || !lines_cap) {
                return EINVAL;
        }
        lseek(fd, 0, SEEK_SET);
        if (!*lines || *lines_cap < 128 * sizeof(struct FileLine)) {
                free(*lines);
                *lines = malloc(128 * sizeof(struct FileLine));
                if (!*lines) {
                        return ENOMEM;
                }
                *lines_cap = 128 * sizeof(struct FileLine);
        }
        char *buf = malloc(1025);
        if (!buf) {
                return ENOMEM;
        }

        (*lines)[0] = (struct FileLine){.location = NONE};

        off_t file_offset = 0;
        ssize_t bytes_read = 0;
        size_t tail_len = 0;
        int i = 1;
        while ((bytes_read = read(fd, buf, 1024)) > 0) {
                buf[1024] = 0;

                off_t buf_position = 0;
                size_t substr_len = 0;
                while (buf_position < bytes_read) {
                        if (i * sizeof(struct FileLine) > *lines_cap - 1) {
                                *lines_cap += 128 * sizeof(struct FileLine);
                                struct FileLine *rlines = realloc(*lines, *lines_cap);
                                if (!rlines) {
                                        return ENOMEM;
                                }
                                *lines = rlines;
                        }
                        if (!tail_len) {
                                const off_t line_offset = file_offset + buf_position;
                                (*lines)[i] = (struct FileLine){
                                        .file_offset = line_offset,
                                        .line_number = i,
                                        .location = FILE_OG
                                };
                        }

                        substr_len = strcspn(buf + buf_position, "\n");
                        if (buf_position + substr_len == bytes_read) {
                                tail_len = substr_len;
                        }
                        else {
                                tail_len = 0;
                                i += 1;
                        }

                        buf_position += substr_len + 1;
                }

                file_offset += bytes_read;
        }
        *lines_size = i;
        while (i * sizeof(struct FileLine) < *lines_cap) {
                (*lines)[i] = (struct FileLine){.location = NONE};
                i += 1;
        }

        lseek(fd, 0, SEEK_SET);
        free(buf);
        return 0;
}

struct FileEditor *create_file_editor(const int fd) {
        bool read_only = false;
        struct FileLine *lines = nullptr;
        size_t lines_size;
        size_t lines_cap = 0;
        if (index_file_lines(fd, &lines, &lines_size, &lines_cap) == ENOMEM) {
                free(lines);
                dprintf(2, "[!] The file is too long to index it's lines, so it is read-only.\n");
                read_only = true;
        }

        struct FileEditor *editor = malloc(sizeof(*editor));
        if (!editor) {
                dprintf(2, "[!] Not enough memory.\n");
                free(lines);
                return nullptr;
        }
        *editor = (struct FileEditor){
                .read_only = read_only,
                .lines = read_only ? nullptr : lines,
                .lines_size = lines_size,
                .lines_cap = lines_cap,
                .file_name = nullptr,
                .fd = fd,
                .fdbak = -1
        };

        return editor;
}

void free_file_editor(struct FileEditor **editor) {
        if (!*editor) {
                return;
        }

        free((*editor)->lines);

        free(*editor);
}

int save_line(const struct FileEditor *editor, struct Line *line) {
        return 0;
}


static ssize_t readline(int fd, char **line_ptr, size_t *line_cap) {
        const int file_pos = lseek(fd, 0, SEEK_CUR);
        size_t buffer_size = 1;

        char *line = malloc(buffer_size);
        memset(line, 0, buffer_size); // set zero in order to guarantee empty string in 1st iteration

        char *newline = nullptr;
        bool eof = false;
        size_t len = 0;
        do {
                constexpr size_t block_size = 80;
                buffer_size += block_size;
                char *rline = realloc(line, buffer_size);
                if (!rline) {
                        free(line);
                        return ENOMEM;
                }
                line = rline;

                const int bytes_read = read(fd, line + strlen(line), block_size);
                if (!bytes_read) {
                        break; //eof
                }
                if (bytes_read < block_size) {
                        eof = true;
                }

                line[buffer_size - 1] = 0;
                len = strlen(line);

                newline = strchr(line, '\n');
        } while (!newline && !eof);

        if (!len) {
                return -1;
        }
        if (newline) {
                len = newline - line + 1;
        }
        line[len] = 0;
        *line_ptr = line;
        if (line_cap) {
                *line_cap = buffer_size;
        }

        lseek(fd, file_pos + len, SEEK_SET);
        return 0;
}

int get_file_line(const struct FileEditor *editor, const unsigned int line_number, struct Line *line) {
        if (editor->lines[line_number].location == NONE) {
                return -1;
        }
        const int destfd = editor->lines[line_number].location == FILE_OG ? editor->fd : editor->fdbak;
        lseek(destfd, editor->lines[line_number].file_offset, SEEK_SET);

        char *line_str = nullptr;
        size_t line_cap = 0;
        readline(destfd, &line_str, &line_cap);
        if (!line_str) {
                return -1;
        }

        *line = (struct Line){
                .line_number = line_number,
                .edited = editor->lines[line_number].location == FILE_OG ? false : true,
                .line = line_str,
                .linecap = line_cap
        };

        return 0;
}

int get_file_lines(struct FileEditor *editor, unsigned int line_number, unsigned int line_count, struct Line **lines) {}

struct Line *new_line_at(struct FileEditor *editor, int line_number) {}

bool check_line_exists(const struct FileEditor *editor, const unsigned int line_number) {
        if (editor->lines[line_number].location == NONE) {
                return false;
        }

        return true;
}
