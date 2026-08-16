//
// Created by Artur Twardzik on 30/03/2026.
//

#ifndef OS_ETHERNET_H
#define OS_ETHERNET_H

#include "kernel/network.h"

struct NetworkInterface *init_ethernet(void);

bool ethernet_check_link_on(struct NetworkInterface *interface);

#endif //OS_ETHERNET_H
