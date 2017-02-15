// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

#define MAX_LOG_DATA 1280

typedef struct logpacket {
    uint32_t magic;
    uint32_t seqno;
    char data[MAX_LOG_DATA];
} logpacket_t;

static const char* appname;

int main(int argc, char** argv) {
    struct sockaddr_in6 addr;
    char tmp[INET6_ADDRSTRLEN];
    int r, s, n = 1;
    uint32_t last_seqno = 0;

    // Make stdout line buffered.
    setvbuf(stdout, NULL, _IOLBF, 0);

    appname = argv[0];

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(33337);

    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "%s: cannot create socket %d\n", appname, s);
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
    if ((r = bind(s, (void*)&addr, sizeof(addr))) < 0) {
        fprintf(stderr, "%s: cannot bind to [%s]%d %d\n", appname,
                inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
                ntohs(addr.sin6_port), r);
        return -1;
    }

    fprintf(stderr, "%s: listening on [%s]%d\n", appname,
            inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
            ntohs(addr.sin6_port));
    for (;;) {
        struct sockaddr_in6 ra;
        socklen_t rlen;
        char buf[4096 + 1];
        logpacket_t* pkt = (void*)buf;
        rlen = sizeof(ra);
        r = recvfrom(s, buf, 4096, 0, (void*)&ra, &rlen);
        if (r < 0) {
            fprintf(stderr, "%s: socket read error %d\n", appname, r);
            break;
        }
        if (r < 8)
            continue;
        if ((ra.sin6_addr.s6_addr[0] != 0xFE) || (ra.sin6_addr.s6_addr[1] != 0x80)) {
            fprintf(stderr, "ignoring non-link-local message\n");
            continue;
        }
        if (pkt->magic != 0xaeae1123)
            continue;
        if (pkt->seqno != last_seqno) {
            buf[r] = 0;
            printf("%s", pkt->data);
            last_seqno = pkt->seqno;
        }
        sendto(s, buf, 8, 0, (struct sockaddr*)&ra, rlen);
    }

    return 0;
}
