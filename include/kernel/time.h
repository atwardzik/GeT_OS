//
// Created by Artur Twardzik on 16/08/2026.
//

#ifndef OS_TIME_H
#define OS_TIME_H

#include "types.h"

time_t sys_time(time_t *tloc);

int sys_stime(const time_t *t);

#endif //OS_TIME_H
