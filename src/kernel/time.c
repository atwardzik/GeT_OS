//
// Created by Artur Twardzik on 16/08/2026.
//
#include "time.h"

#include "errno.h"
#include "drivers/time.h"

static struct {
        uint64_t inner_time;
        time_t sntp_unix_timestamp;
} timestamp = {.inner_time = 0, .sntp_unix_timestamp = 0};

time_t sys_time(time_t *tloc) {
        uint64_t current_time;
        ms_since_boot(&current_time);

        const uint32_t delta_time = current_time - timestamp.inner_time;

        return timestamp.sntp_unix_timestamp + delta_time / 1000;
}

int sys_stime(const time_t *t) {
        ms_since_boot(&timestamp.inner_time);
        timestamp.sntp_unix_timestamp = *t;

        return 0;
}
