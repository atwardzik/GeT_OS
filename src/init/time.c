//
// Created by Artur Twardzik on 05/11/2026.
//
#include "init.h"

#include "drivers/time.h"
#include "libc.h"

//TODO: save current time on shutdown and retrieve it from file if the internet connection is off


#define NTP_UNIX_EPOCH_DIFF 2208988800UL

void set_current_time(void) {
        printf("\x1b[96;49m[!] Retrieving current time. \x1b[0m");

        time_t unix_time = 0;

        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
                dprintf(2, "Error while trying to open socket\n");
                stime(&unix_time);
                return;
        }
        struct sockaddr_in source = {AF_INET, htons(2512)};
        bind(sockfd, (struct sockaddr *) &source, sizeof(source));

        struct sockaddr_in dest = {AF_INET, htons(123)};
        inet_aton("192.168.1.57", &dest.sin_addr);
        // inet_aton("216.239.35.0", &dest.sin_addr); // time.google.com

        if (connect(sockfd, (struct sockaddr *) &dest, sizeof(dest)) < 0) {
                dprintf(2, "Error while trying to connect - connection timed out\n");
                close(sockfd);
                stime(&unix_time);
                return;
        }

        uint8_t buffer1[56];
        uint8_t buffer2[56];
retry:
        buffer1[0] = 0x23;
        buffer2[0] = 0x23;

        write(sockfd, buffer1, 48);
        read(sockfd, buffer1, sizeof(buffer1));
        uint32_t timestamp1 = 0;
        for (int i = 0; i < 4; ++i) {
                timestamp1 = (timestamp1 << 8) | buffer1[48 + i];
        }

        delay_ms(1);

        write(sockfd, buffer2, 48);
        read(sockfd, buffer2, sizeof(buffer2));
        uint32_t timestamp2 = 0;
        for (int i = 0; i < 4; ++i) {
                timestamp2 = (timestamp2 << 8) | buffer2[48 + i];
        }

        if (timestamp2 - timestamp1 > 1) { //TODO: add timeout
                goto retry;
        }

        unix_time = timestamp1 - NTP_UNIX_EPOCH_DIFF;
        stime(&unix_time);

        close(sockfd);

        const time_t current_time = time(nullptr);
        printf("Current time is: %s\n", ctime(&current_time));
}
