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
        char *buffer = malloc(st.st_size + 1);
        if (!buffer) {
                return nullptr;
        }
        memset(buffer, 0, st.st_size + 1);
        if (read(fd, buffer, st.st_size) <= 0) {
                free(buffer);
                return nullptr;
        }

        close(fd);
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

                char *new_html = malloc(new_len + 1);
                if (!new_html) {
                        return -ENOMEM;
                }
                memset(new_html, 0, new_len + 1);

                memcpy(new_html, *html, index);             // until token occurrence
                memcpy(new_html + index, value, value_len); // token
                memcpy(new_html + index + value_len,        //
                       *html + index + token_len,           // rest
                       current_len - index - token_len + 1  //
                );

                free(*html);
                *html = new_html;
        }

        return 0;
}

static int replace_server_token(char **html, const char *token) {
        if (strcmp(token, "${{TIME}}") == 0) {
                const time_t current_time = time(nullptr);
                const char *time_fmt = ctime(&current_time);

                replace_in_html(html, token, time_fmt);
        }

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
        int rc = 0;

        printf("Connection accepted\n");

        char *website = read_html("index.htm");
        if (!website) {
                rc = -ENOENT;
                goto conn_close;
        }
        replace_server_tokens(&website);
        const int html_length = strlen(website);

        const int header_len = strlen(http_header) - 2                    // -2 for "%i"
                               + snprintf(nullptr, 0, "%i", html_length); //
        const int http_response_length = header_len + strlen(website) + 1;

        char *http_response = malloc(http_response_length);
        if (!http_response) {
                rc = -ENOMEM;
                goto conn_close;
        }
        memset(http_response, 0, http_response_length);
        snprintf(http_response, http_response_length, http_header, strlen(website));
        memmove(http_response + strlen(http_response), website, html_length);

        write(sockfd, http_response, strlen(http_response));
        free(http_response);

conn_close:
        free(website);

        return (void *) (intptr_t) rc;
}


int main(int argc, char *argv[]) {
        while (1) {
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                        dprintf(2, "Error while trying to open socket\n");
                        continue;
                }

                struct sockaddr_in source = {.sin_family = AF_INET, .sin_addr.s_addr = 0, .sin_port = htons(8080)};
                if (bind(sockfd, (struct sockaddr *) &source, sizeof(source)) < 0) {
                        dprintf(2, "Error while trying to bind\n");
                        continue;
                }


                if (listen(sockfd, 0) < 0) {
                        dprintf(2, "Error while trying to listen on port: %i\n", source.sin_port);
                        continue;
                }

                struct sockaddr destination = {};
                const size_t dest_len = sizeof(destination);

                int destfd;
                if ((destfd = accept(sockfd, &destination, dest_len)) < 0) {
                        dprintf(2, "Error while trying to accept connection\n");
                        close(sockfd);
                        continue;
                }

                const int rc = (intptr_t) manage_connection((void *) (intptr_t) destfd);
                printf("Connection closed, rc: %i\n", rc);
                close(sockfd);
        }

        return 0;
}
