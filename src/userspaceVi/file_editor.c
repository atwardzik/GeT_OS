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
        uint16_t line_len;

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
                size_t line_len = 0;
                while (buf_position < bytes_read) {
                        if (i * sizeof(struct FileLine) > *lines_cap - 1) {
                                *lines_cap += 128 * sizeof(struct FileLine);
                                struct FileLine *rlines = realloc(*lines, *lines_cap);
                                if (!rlines) {
                                        return ENOMEM;
                                }
                                *lines = rlines;
                        }
                        if (!tail_len && line_len) {
                                const off_t line_offset = file_offset + buf_position - line_len;
                                (*lines)[i - 1] = (struct FileLine){
                                        .file_offset = line_offset,
                                        .line_number = i - 1,
                                        .line_len = line_len,
                                        .location = FILE_OG
                                };
                        }

                        char *line = buf + buf_position;
                        line_len = strcspn(buf + buf_position, "\n") + 1;
                        buf_position += line_len;
                        if (buf_position >= bytes_read) {
                                tail_len = line_len - 1;
                        }
                        else {
                                line_len += tail_len;
                                tail_len = 0;
                                i += 1;
                        }
                }

                file_offset += bytes_read;
        }
        if (tail_len) {
                const off_t line_offset = file_offset - tail_len - 1;
                (*lines)[i] = (struct FileLine){
                        .file_offset = line_offset,
                        .line_number = i,
                        .line_len = tail_len,
                        .location = FILE_OG
                };
                i += 1;
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

int get_file_line(const struct FileEditor *editor, const unsigned int line_number, struct Line *line) {
        if (editor->lines[line_number].location == NONE) {
                return -1;
        }
        const int destfd = editor->lines[line_number].location == FILE_OG ? editor->fd : editor->fdbak;
        lseek(destfd, editor->lines[line_number].file_offset, SEEK_SET);

        const int line_str_len = editor->lines[line_number].line_len;
        char *line_str = malloc(line_str_len + 1);
        if (!line_str) {
                return -1;
        }
        read(destfd, line_str, line_str_len);
        line_str[line_str_len] = 0;

        *line = (struct Line){
                .line_number = line_number,
                .edited = editor->lines[line_number].location == FILE_OG ? false : true,
                .line = line_str,
                .linecap = line_str_len
        };

        return 0;
}

int get_file_lines(
        const struct FileEditor *editor, const unsigned int line_number, const unsigned int line_count,
        struct Line *lines
) {
        for (unsigned int i = line_number; i < line_number + line_count; ++i) {
                if (editor->lines[i].location == NONE) {
                        return -1;
                }
        }

        unsigned int previous_file_lines_count = 0;
        unsigned int i = 0;
        do {
                unsigned int file_bytes_count = 0;
                const struct FileLine *first_line = &editor->lines[line_number + i];
                while (editor->lines[line_number + i].location == first_line->location && i < line_count) {
                        file_bytes_count += editor->lines[line_number + i].line_len;
                        i += 1;
                }

                char *buf = malloc(file_bytes_count);
                if (!buf) {
                        return -1;
                }

                const int destfd = first_line->location == FILE_OG ? editor->fd : editor->fdbak;
                lseek(destfd, first_line->file_offset, SEEK_SET);
                read(destfd, buf, file_bytes_count);

                unsigned int buf_offset = 0;
                for (unsigned int j = 0; j < i - previous_file_lines_count; ++j) {
                        const int line_len = editor->lines[line_number + j].line_len;
                        char *line_str = malloc(line_len + 1);
                        if (!line_str) {
                                return j;
                        }
                        memcpy(line_str, buf + buf_offset, line_len);
                        line_str[line_len] = 0;

                        lines[j] = (struct Line){
                                line_str,
                                line_len + 1,
                                line_number + j,
                                first_line->location == FILE_OG ? false : true,
                        };

                        buf_offset += line_len;
                }

                previous_file_lines_count = i;

                free(buf);
        } while (i < line_count);

        return i;
}

struct Line *new_line_at(struct FileEditor *editor, int line_number) {}

bool check_line_exists(const struct FileEditor *editor, const unsigned int line_number) {
        if (editor->lines[line_number].location == NONE) {
                return false;
        }

        return true;
}
