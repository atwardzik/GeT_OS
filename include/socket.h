//
// Created by Artur Twardzik on 07/04/2026.
//

#ifndef OS_SOCKET_H
#define OS_SOCKET_H

#define AF_INET		2	/* Internet IP Protocol         */

#define SOCK_STREAM	1	/* stream (connection) socket	*/
#define SOCK_DGRAM	2	/* datagram (conn.less) socket	*/
#define SOCK_RAW	3	/* raw socket			*/


struct in_addr {
        uint32_t s_addr;
};

struct sockaddr_in {
        int16_t sin_family;
        uint16_t sin_port;
        struct in_addr sin_addr;
        char sin_zero[8];
};

struct sockaddr {
        uint16_t sa_family;
        char sa_data[14];
};


int socket(int domain, int type, int protocol);

int bind(int sockfd, const struct sockaddr *addr, size_t addrlen);

int listen(int sockfd, int backlog);

int accept(int sockfd, struct sockaddr *addr, size_t addrlen);

int connect(int sockfd, const struct sockaddr *addr, size_t adrlen);

uint16_t htons(uint16_t hostshort);

int inet_aton(const char *host_address, struct in_addr *inp);


#endif //OS_SOCKET_H
