// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/compiler.h>

typedef struct {
    icmphdr hdr;
    uint8_t payload[32];
} __PACKED packet_t;

int usage() {
    fprintf(stderr, "usage: ping [ <option>* ] destination\n");
    fprintf(stderr, "Send ICMP ECHO_REQUEST to a destination. This destination\n");
    fprintf(stderr, "may be a hostname (google.com) or an IP address (8.8.8.8).\n");
    fprintf(stderr, "-c count: Only receive count packets (default = 3)\n");
    fprintf(stderr, "--help: View this help message\n");
    return -1;
}

int main(int argc, const char** argv) {
    size_t count = 3;
    argv++;
    argc--;
    while (argc > 1) {
        if (!strncmp(argv[0], "-c", strlen("-c"))) {
            argv++;
            argc--;
            if (argc < 1) {
                return usage();
            }
            char* endptr;
            count = strtol(argv[0], &endptr, 10);
            if (*endptr != '\0') {
                return usage();
            }
            argv++;
            argc--;
        } else {
            return usage();
        }
    }

    if (argc == 0 || !strcmp(argv[0], "--help")) {
        return usage();
    }

    const char* host = argv[0];

    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (s < 0) {
        fprintf(stderr, "Could not acquire ICMP socket: %d\n", errno);
        return -1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_ICMP;
    struct addrinfo* info;
    if (getaddrinfo(host, NULL, &hints, &info)) {
        fprintf(stderr, "ping: unknown host %s\n", host);
        return -1;
    }

    struct sockaddr* saddr = info->ai_addr;
    char buf[256];
    if (saddr->sa_family == AF_INET) {
        struct sockaddr_in* iaddr = reinterpret_cast<struct sockaddr_in*>(saddr);
        inet_ntop(saddr->sa_family, &iaddr->sin_addr, buf, sizeof(buf));
    } else {
        struct sockaddr_in6* iaddr = reinterpret_cast<struct sockaddr_in6*>(saddr);
        inet_ntop(saddr->sa_family, &iaddr->sin6_addr, buf, sizeof(buf));
    }

    printf("PING %s (%s)\n", host, buf);

    uint16_t sequence = 1;
    packet_t packet;
    ssize_t r;
    const zx_ticks_t ticks_per_usec = zx_ticks_per_second() / 1000000;

    while (count-- > 0) {
        memset(&packet, 0, sizeof(packet));
        packet.hdr.type = ICMP_ECHO;
        packet.hdr.code = 0;
        packet.hdr.un.echo.id = 0;
        packet.hdr.un.echo.sequence = htons(sequence++);
        constexpr char kMessage[] = "This is an echo message!";
        strcpy(reinterpret_cast<char *>(packet.payload), kMessage);
        // Netstack will overwrite the checksum
        zx_ticks_t before = zx_ticks_get();
        r = sendto(s, &packet, sizeof(packet.hdr) + strlen(kMessage) + 1, 0,
                   saddr, sizeof(*saddr));
        if (r < 0) {
            fprintf(stderr, "ping: Could not send packet\n");
            return -1;
        }

        struct pollfd fd;
        fd.fd = s;
        fd.events = POLLIN;
        switch (poll(&fd, 1, 1000)) {
        case 1:
            if (fd.revents & POLLIN) {
                r = recvfrom(s, &packet, sizeof(packet), 0, NULL, NULL);
                break;
            } else {
                fprintf(stderr, "ping: Spurious wakeup from poll\n");
                r = -1;
                break;
            }
        case 0:
            fprintf(stderr, "ping: Timed out after one second\n");
        default:
            r = -1;
        }

        if (r < 0) {
            fprintf(stderr, "ping: Could not read result of ping\n");
            return -1;
        }
        zx_ticks_t after = zx_ticks_get();
        int seq = ntohs(packet.hdr.un.echo.sequence);
        uint64_t usec = (after - before) / ticks_per_usec;
        printf("%" PRIu64 " bytes: icmp_seq=%d time=%" PRIu64 " us\n", r, seq, usec);
        if (count > 0) {
            sleep(1);
        }
    }
    freeaddrinfo(info);
    close(s);
    return 0;
}
