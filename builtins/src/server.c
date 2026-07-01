#include "errno.h"
#include "libc.h"
#include "socket.h"


#define EXIT_FAILURE 1

static const char *http_header = "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %i\r\n"
        "Connection: close\r\n\r\n";

const char *server_tokens[] = {"${{TIME}}", "${{TEST}}"};

static char *read_html(const char *filename) {
        const int fd = open(filename, O_RDONLY, 0);
        if (fd < 0) {
                return nullptr;
        }

        struct stat st;
        fstat(fd, &st);
        char *buffer = malloc(st.st_size);
        if (!buffer) {
                return nullptr;
        }
        if (read(fd, buffer, st.st_size) <= 0) {
                free(buffer);
                return nullptr;
        }

        return buffer;
}

[[maybe_unused]]
static int replace_in_html(char **html, const char *token, const char *value) {
        const size_t token_len = strlen(token);
        const size_t value_len = strlen(value);

        char *substring;
        while ((substring = strstr(*html, token)) != nullptr) {
                const size_t current_len = strlen(*html);
                const size_t index = substring - *html;
                const size_t new_len = current_len - token_len + value_len;

                char *new_html = realloc(*html, new_len + 1);
                if (!new_html) {
                        return -ENOMEM;
                }
                *html = new_html;

                memmove(*html + index + value_len, *html + index + token_len, current_len - index - token_len + 1);
                memcpy(*html + index, value, value_len);
        }

        return 0;
}

static int replace_server_token(char **html, const char *token) {
#ifdef TARGET_LINUX
        if (strcmp(token, "${{TIME}}") == 0) {
                struct timeval tv;
                gettimeofday(&tv, nullptr);

                const struct tm *time = localtime(&tv.tv_sec);

                char datetime[80];

                strftime(datetime, sizeof(datetime), "%a %d %b %Y %H:%M:%S", time);

                replace_in_html(html, token, datetime);
        }
#endif

        return 0;
}

static int replace_server_tokens(char **html) {
        constexpr int tokens_count = sizeof(server_tokens) / sizeof(*server_tokens);
        for (size_t i = 0; i < tokens_count; ++i) {
                replace_server_token(html, server_tokens[i]);
        }

        return 0;
}

static void *manage_connection(void *arg) {
        const int sockfd = (int) (intptr_t) arg;
        int ret = 0;

        printf("Connection accepted\n");

        char *website = read_html("/mnt/disk0/index.htm");
        if (!website) {
                ret = -ENOENT;
                goto conn_close;
        }
        replace_server_tokens(&website);
        const int html_length = strlen(website);

        const int header_len = strlen(http_header) - 2 + snprintf(nullptr, 0, "%i", html_length); // -2 for %i
        const int http_response_length = header_len + strlen(website) + 1;

        char *http_response = realloc(website, http_response_length);
        if (!http_response) {
                ret = -ENOMEM;
                goto conn_close;
        }
        memmove(http_response + header_len, website, html_length);
        snprintf(http_response, http_response_length, http_header, strlen(website));

        write(sockfd, http_response, strlen(http_response));
        free(http_response);

conn_close:
        free(website);
        close(sockfd);

        return (void *) (intptr_t) ret;
}

static int event_loop(void) {
        while (1) {
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                        dprintf(2, "Error while trying to open socket\n");
                        // return EXIT_FAILURE;
                }

                struct sockaddr_in source = {.sin_family = AF_INET, .sin_addr.s_addr = 0, .sin_port = htons(8080)};
                if (bind(sockfd, (struct sockaddr *) &source, sizeof(source)) < 0) {
                        dprintf(2, "Error while trying to bind\n");
                        // return EXIT_FAILURE;
                }


                if (listen(sockfd, 0) < 0) {
                        dprintf(2, "Error while trying to listen on port: %i\n", source.sin_port);
                        // return EXIT_FAILURE;
                }

                struct sockaddr destination = {};
                const size_t dest_len = sizeof(destination);

                int destfd;
                if ((destfd = accept(sockfd, &destination, dest_len)) < 0) {
                        dprintf(2, "Error while trying to accept connection\n");
                        close(sockfd);
                        continue;
                }

                manage_connection((void *) (intptr_t) destfd);
        }

        return 0;
}


int main(int argc, char *argv[]) {
        event_loop();

        return 0;
}
