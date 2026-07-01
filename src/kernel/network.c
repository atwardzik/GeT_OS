//
// Created by Artur Twardzik on 01/04/2026.
//

#include "network.h"
#include "errno.h"
#include "error.h"
#include "memory.h"
#include "proc.h"
#include "socket.h"
#include "tty.h"
#include "drivers/ethernet.h"

static constexpr size_t MAX_INTERFACES_COUNT = 1;

static constexpr size_t ETHERNET_HEADER_LENGTH = 14;

// struct Sockfs_Inode {
//         struct VFS_Inode vfs_inode;
//
//         struct Socket *socket;
// };

static struct {
        struct NetworkInterface **interfaces;

        // struct Sockfs_Inode *sockfs_root;
} network_manager __attribute__((section(".data")));

int str2mac(const char *src_mac, char *buf) {
        if (!src_mac || !buf) {
                return -EINVAL;
        }
        if (strlen(src_mac) != 17) {
                return -EINVAL;
        }

        char mac[18];
        strcpy(mac, src_mac);

        const char *byte = strtok(mac, ":");
        int i = 0;
        while (i < 6) {
                if (strlen(byte) != 2) {
                        return -EINVAL;
                }

                buf[i] = strtoul(byte, nullptr, 16);

                byte = strtok(nullptr, ":");
                i += 1;
        }

        return 0;
}

int str2ip(const char *src_ip, char *buf) {
        if (!src_ip || !buf) {
                return -EINVAL;
        }
        if (strlen(src_ip) > 15) {
                return -EINVAL;
        }

        char ip[18];
        strcpy(ip, src_ip);

        const char *byte = strtok(ip, ".");
        int i = 0;
        while (i < 4) {
                if (strlen(byte) > 3) {
                        return -EINVAL;
                }
                const unsigned int octet = strtoul(byte, nullptr, 10);
                if (octet > 255) {
                        return -EINVAL;
                }

                buf[i] = octet;

                byte = strtok(nullptr, ".");
                i += 1;
        }

        return 0;
}

//TODO: make it ioctl
static int setup_network_information(
        struct NetworkInterface *interface, const char *ip_address, const char *mac_address, const char *gateway,
        const char *subnet_mask
) {
        char mac[6];
        if (str2mac(mac_address, mac) == 0) {
                memcpy(interface->mac, mac, sizeof(mac));
        }

        char gw[4];
        if (str2ip(gateway, gw) == 0) {
                memcpy(interface->gateway, gw, sizeof(gw));
        }

        char sub[4];
        if (str2ip(subnet_mask, sub) == 0) {
                memcpy(interface->subnet_mask, sub, sizeof(sub));
        }

        char ip[4];
        if (str2ip(ip_address, ip) == 0) {
                memcpy(interface->ip, ip, sizeof(ip));
        }

        return interface->setup_network_information(interface);
}

int init_network(void) {
        struct NetworkInterface **interfaces = kmalloc(MAX_INTERFACES_COUNT * sizeof(*interfaces));
        if (!interfaces) {
                return -ENOMEM;
        }

        printk_status_init("Checking network adapter");
        struct NetworkInterface *eth0 = init_ethernet();
        if (IS_ERR(eth0)) {
                printk_status_finish(-ENETDOWN);
                return -ENETDOWN;
        }
        interfaces[0] = eth0;
        printk_status_finish(0);


        printk_status_init("Setting up network adapter");
        // TODO: dhcp protocol
        const char *ip_addr = "192.168.2.1";
        const char *mac_addr = "de:ad:01:10:be:ef";
        const int res = setup_network_information(eth0,
                                                  ip_addr,
                                                  mac_addr,
                                                  "192.168.1.1",
                                                  "255.255.255.0"
        );


        network_manager.interfaces = interfaces;
        printk_status_finish(res);
        printk_status_info("Network adapter set up to: 192.168.2.1 de:ad:01:10:be:ef");

        return 0;
}

int sys_socket(int domain, int type, int protocol) {
        if (domain != AF_INET) {
                return -EAFNOSUPPORT;
        }

        struct Process *current_process = scheduler_get_current_process();
        if (current_process->files.count >= MAX_OPEN_FILE_DESCRIPTORS) {
                return -EMFILE;
        }

        // TODO: make it dependent on network interfaces connected
        struct Socket *socket = nullptr;
        int res;
        switch (type) {
                case SOCK_STREAM:
                        socket = network_manager.interfaces[0]->create_socket();
                        if (IS_ERR(socket)) {
                                return -ENOSOCKFREE;
                        }
                        res = socket->s_op->open(socket, TCP);
                        break;
                case SOCK_RAW:
                        socket = network_manager.interfaces[0]->create_raw_socket();
                        if (IS_ERR(socket)) {
                                return -ENOSOCKFREE;
                        }
                        res = socket->s_op->open(socket, MACRAW);
                        break;
                default:
                        return -EPROTONOSUPPORT;
        }

        if (res < 0) {
                return res;
        }

        for (size_t i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; ++i) {
                if (current_process->files.fdtable[i] == nullptr) {
                        current_process->files.fdtable[i] = &socket->file;
                        current_process->files.count += 1;

                        return i;
                }
        }

        return -ENOSOCKFREE;
}

int sys_bind(int sockfd, const struct sockaddr *addr, size_t addrlen) {
        const struct Process *current_process = scheduler_get_current_process();
        if (sockfd >= current_process->files.count || sockfd < 0 || current_process->files.fdtable[sockfd] == nullptr) {
                return -EBADF;
        }

        struct File *socket_file = current_process->files.fdtable[sockfd];
        struct Socket *socket = (struct Socket *) socket_file;

        // const uint16_t port = (addr->sa_data[1] << 8) | (addr->sa_data[0]); // FIXME: endianess dependent
        return socket->s_op->bind(socket, *(uint16_t *) &addr->sa_data);
}

int sys_listen(int sockfd, [[maybe_unused]] int backlog) {
        const struct Process *current_process = scheduler_get_current_process();
        if (sockfd >= current_process->files.count || sockfd < 0 || current_process->files.fdtable[sockfd] == nullptr) {
                return -EBADF;
        }

        struct File *socket_file = current_process->files.fdtable[sockfd];
        struct Socket *socket = (struct Socket *) socket_file;

        return socket->s_op->listen(socket);
}

int sys_accept(int sockfd, struct sockaddr *addr, size_t addrlen) {
        const struct Process *current_process = scheduler_get_current_process();
        if (sockfd >= current_process->files.count || sockfd < 0 || current_process->files.fdtable[sockfd] == nullptr) {
                return -EBADF;
        }

        struct File *socket_file = current_process->files.fdtable[sockfd];
        struct Socket *socket = (struct Socket *) socket_file;

        int res = socket->s_op->accept(socket, addr, addrlen);
        return res < 0 ? res : sockfd; // for POSIX compliancy, return socket number (even though it is the same socket)
}

int sys_connect(int sockfd, const struct sockaddr *addr, size_t addrlen) {
        const struct Process *current_process = scheduler_get_current_process();
        if (sockfd >= current_process->files.count || sockfd < 0 || current_process->files.fdtable[sockfd] == nullptr) {
                sys_write(1, "[!] There is no such socket descriptor\n", 39);
                __asm__("bkpt   #0");
                return -1;
        }

        struct File *socket_file = current_process->files.fdtable[sockfd];
        struct Socket *socket = (struct Socket *) socket_file;

        return socket->s_op->connect(socket, addr, addrlen);
}
