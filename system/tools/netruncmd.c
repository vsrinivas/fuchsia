// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ifaddrs.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

#include <system/netboot.h>

static const char* appname;

#define MAXSIZE 1024

typedef struct {
    struct nbmsg_t hdr;
    uint8_t data[MAXSIZE];
} msg;

int main(int argc, char** argv) {
    struct sockaddr_in6 addr;
    int r, s;

    appname = argv[0];

    if (argc < 3) {
        fprintf(stderr, "usage: %s <hostname> <command>\n", appname);
        return -1;
    }

    const char* hostname = argv[1];
    if (!strcmp(hostname, "-")) {
        hostname = "*";
    }
    if (!strcmp(hostname, ":")) {
        hostname = "*";
    }

    size_t hostname_len = strlen(hostname) + 1;
    if (hostname_len > MAXSIZE) {
        fprintf(stderr, "%s: hostname too long\n", appname);
        return -1;
    }


    char cmd[MAXSIZE];
    size_t cmd_len = 0;
    while (argc > 2) {
        size_t len = strlen(argv[2]);
        if (len > (MAXSIZE - cmd_len - 1)) {
            fprintf(stderr, "%s: command too long\n", appname);
            return -1;
        }
        memcpy(cmd + cmd_len, argv[2], len);
        cmd_len += len;
        cmd[cmd_len++] = ' ';
        argc--;
        argv++;
    }
    cmd[cmd_len - 1] = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(NB_SERVER_PORT);
    inet_pton(AF_INET6, "ff02::1", &addr.sin6_addr);
    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "%s: cannot create socket %d\n", appname, s);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 250 * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint32_t cookie = 0x12345678;

    msg m;
    m.hdr.magic = NB_MAGIC;
    m.hdr.cookie = cookie;
    m.hdr.cmd = NB_QUERY;
    m.hdr.arg = 0;
    memcpy(m.data, hostname, hostname_len);

    struct ifaddrs* ifa;
    if (getifaddrs(&ifa) < 0) {
        fprintf(stderr, "%s: cannot enumerate network interfaces\n", appname);
        return -1;
    }

    for (int i = 0; i < 5; i++) {
        // transmit query on all local links
        for (; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr->sa_family != AF_INET6) {
                continue;
            }
            struct sockaddr_in6* in6 = (void*)ifa->ifa_addr;
            if (in6->sin6_scope_id == 0) {
                continue;
            }
            // printf("tx %s (sid=%d)\n", ifa->ifa_name, in6->sin6_scope_id);
            size_t sz = sizeof(nbmsg) + hostname_len;
            addr.sin6_scope_id = in6->sin6_scope_id;
            if ((r = sendto(s, &m, sz, 0, (struct sockaddr*)&addr, sizeof(addr))) != sz) {
                fprintf(stderr, "%s: cannot send %d %s\n", appname, errno, strerror(errno));
            }
        }

        // listen for replies
        struct sockaddr_in6 ra;
        socklen_t rlen = sizeof(ra);
        memset(&ra, 0, sizeof(ra));
        int r = recvfrom(s, &m, sizeof(m), 0, (void*)&ra, &rlen);
        if (r > sizeof(nbmsg)) {
            r -= sizeof(nbmsg);
            m.data[r] = 0;
            if ((m.hdr.magic == NB_MAGIC) &&
                (m.hdr.cookie == cookie) &&
                (m.hdr.cmd == NB_ACK)) {
                char tmp[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)) == NULL) {
                    strcpy(tmp,"???");
                }
                printf("found %s at %s/%d\n", (char*)m.data, tmp, ra.sin6_scope_id);
                ra.sin6_port = htons(NB_SERVER_PORT);
                m.hdr.magic = NB_MAGIC;
                m.hdr.cookie = cookie + 1;
                m.hdr.cmd = NB_SHELL_CMD;
                m.hdr.arg = 0;
                memcpy(m.data, cmd, cmd_len);
                sendto(s, &m, sizeof(nbmsg) + cmd_len, 0, (struct sockaddr*)&ra, sizeof(ra));
                return 0;
            }
        }
    }
    fprintf(stderr, "%s: timed out\n", appname);
    return -1;
}
