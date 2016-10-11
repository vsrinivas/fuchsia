// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

#include <magenta/netboot.h>

static uint32_t cookie = 1;
static char* appname;

static int io(int s, nbmsg* msg, size_t len, nbmsg* ack) {
    int retries = 5;
    int r;

    msg->magic = NB_MAGIC;
    msg->cookie = cookie++;

    for (;;) {
        r = write(s, msg, len);
        if (r < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            }
            fprintf(stderr, "\n%s: socket write error %d\n", appname, errno);
            return -1;
        }
    again:
        r = read(s, ack, 2048);
        if (r < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                retries--;
                if (retries > 0) {
                    fprintf(stderr, "T");
                    continue;
                }
                fprintf(stderr, "\n%s: timed out\n", appname);
            } else {
                fprintf(stderr, "\n%s: socket read error %d\n", appname, errno);
            }
            return -1;
        }
        if (r < sizeof(nbmsg)) {
            fprintf(stderr, "Z");
            goto again;
        }
        if (ack->magic != NB_MAGIC) {
            fprintf(stderr, "?");
            goto again;
        }
        if (ack->cookie != msg->cookie) {
            fprintf(stderr, "C");
            goto again;
        }
        if (ack->arg != msg->arg) {
            fprintf(stderr, "A");
            goto again;
        }
        if (ack->cmd == NB_ACK)
            return 0;
        fprintf(stderr, "?");
        goto again;
    }
}

typedef struct {
    FILE* fp;
    const char *data;
    size_t datalen;
} xferdata;

static ssize_t xread(xferdata *xd, void* data, size_t len) {
    if (xd->fp == NULL) {
        if (len > xd->datalen) {
            len = xd->datalen;
        }
        memcpy(data, xd->data, len);
        xd->datalen -= len;
        xd->data += len;
        return len;
    } else {
        ssize_t r = fread(data, 1, len, xd->fp);
        if (r == 0) {
            return ferror(xd->fp) ? -1 : 0;
        }
        return r;
    }
}

static int xfer(struct sockaddr_in6* addr, const char* fn, const char* name, bool boot) {
    xferdata xd;
    char msgbuf[2048];
    char ackbuf[2048];
    char tmp[INET6_ADDRSTRLEN];
    struct timeval tv;
    struct timeval begin, end;
    nbmsg* msg = (void*)msgbuf;
    nbmsg* ack = (void*)ackbuf;
    int s, r;
    int count = 0;
    int status = -1;

    if (!strcmp(fn, "(cmdline)")) {
        xd.fp = NULL;
        xd.data = name;
        xd.datalen = strlen(name) + 1;
        name = "cmdline";
    } else if ((xd.fp = fopen(fn, "rb")) == NULL) {
        fprintf(stderr, "%s: could not open file %s\n", appname, fn);
        return -1;
    }
    if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr, "%s: cannot create socket %d\n", appname, errno);
        goto done;
    }
    fprintf(stderr, "%s: sending '%s'...\n", appname, fn);
    gettimeofday(&begin, NULL);
    tv.tv_sec = 0;
    tv.tv_usec = 250 * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(s, (void*)addr, sizeof(*addr)) < 0) {
        fprintf(stderr, "%s: cannot connect to [%s]%d\n", appname,
                inet_ntop(AF_INET6, &addr->sin6_addr, tmp, sizeof(tmp)),
                ntohs(addr->sin6_port));
        goto done;
    }

    msg->cmd = NB_SEND_FILE;
    msg->arg = 0;
    strcpy((void*)msg->data, name);
    if (io(s, msg, sizeof(nbmsg) + strlen(name) + 1, ack)) {
        fprintf(stderr, "%s: failed to start transfer\n", appname);
        goto done;
    }

    msg->cmd = NB_DATA;
    msg->arg = 0;
    char* nl = "";
    do {
        r = xread(&xd, msg->data, 1024);
        if (r < 0) {
            fprintf(stderr, "%s%s: error: reading '%s'\n", nl, appname, fn);
            goto done;
        }
        if (r == 0) {
            break;
        }
        count += r;
        if (count >= (32 * 1024)) {
            count = 0;
            fprintf(stderr, "#");
            nl = "\n";
        }
        if (io(s, msg, sizeof(nbmsg) + r, ack)) {
            fprintf(stderr, "%s%s: error: sending '%s'\n", nl, appname, fn);
            goto done;
        }
        msg->arg += r;
    } while (r != 0);

    status = 0;

    if (boot) {
        msg->cmd = NB_BOOT;
        msg->arg = 0;
        if (io(s, msg, sizeof(nbmsg), ack)) {
            fprintf(stderr, "%s%s: failed to send boot command\n", nl, appname);
        } else {
            fprintf(stderr, "%s%s: sent boot command\n", nl, appname);
        }
    } else {
        fprintf(stderr, "%s", nl);
    }
done:
    gettimeofday(&end, NULL);
    if (end.tv_usec < begin.tv_usec) {
        end.tv_sec -= 1;
        end.tv_usec += 1000000;
    }
    fprintf(stderr, "%s: %d.%06d sec\n\n", appname, (int)(end.tv_sec - begin.tv_sec),
            (int)(end.tv_usec - begin.tv_usec));
    if (s >= 0) {
        close(s);
    }
    if (xd.fp != NULL) {
        fclose(xd.fp);
    }
    return status;
}

void usage(void) {
    fprintf(stderr,
            "usage:   %s [ <option> ]* <kernel> [ <ramdisk> ] [ -- [ <kerneloption> ]* ]\n"
            "\n"
            "options: -1  only boot once, then exit\n",
            appname);
    exit(1);
}

void drain(int fd) {
    char buf[4096];
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
        while (read(fd, buf, sizeof(buf)) > 0)
            ;
        fcntl(fd, F_SETFL, 0);
    }
}

int main(int argc, char** argv) {
    struct sockaddr_in6 addr;
    char tmp[INET6_ADDRSTRLEN];
    char cmdline[4096];
    char *cmdnext = cmdline;
    int r, s, n = 1;
    const char* kernel_fn = NULL;
    const char* ramdisk_fn = NULL;
    int once = 0;
    int status;

    cmdline[0] = 0;
    if ((appname = strrchr(argv[0], '/')) != NULL) {
        appname++;
    } else {
        appname = argv[0];
    }

    while (argc > 1) {
        if (argv[1][0] != '-') {
            if (kernel_fn == NULL) {
                kernel_fn = argv[1];
            } else if (ramdisk_fn == NULL) {
                ramdisk_fn = argv[1];
            } else {
                usage();
            }
        } else if (!strcmp(argv[1], "-1")) {
            once = 1;
        } else if (!strcmp(argv[1], "--")) {
            while (argc > 2) {
                size_t len = strlen(argv[2]);
                if (len > (sizeof(cmdline) - 2 - (cmdnext - cmdline))) {
                    fprintf(stderr, "%s: commandline too large\n", appname);
                    return -1;
                }
                if (cmdnext != cmdline) {
                    *cmdnext++ = ' ';
                }
                memcpy(cmdnext, argv[2], len + 1);
                cmdnext += len;
                argc--;
                argv++;
            }
            break;
        } else {
            usage();
        }
        argc--;
        argv++;
    }
    if (kernel_fn == NULL) {
        usage();
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(NB_ADVERT_PORT);

    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "%s: cannot create socket %d\n", appname, s);
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
    if ((r = bind(s, (void*)&addr, sizeof(addr))) < 0) {
        fprintf(stderr, "%s: cannot bind to [%s]%d %d: %s\n", appname,
                inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
                ntohs(addr.sin6_port), errno, strerror(errno));
        return -1;
    }

    fprintf(stderr, "%s: listening on [%s]%d\n", appname,
            inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
            ntohs(addr.sin6_port));

    for (;;) {
        struct sockaddr_in6 ra;
        socklen_t rlen;
        char buf[4096];
        nbmsg* msg = (void*)buf;
        rlen = sizeof(ra);
        r = recvfrom(s, buf, 4096, 0, (void*)&ra, &rlen);
        if (r < 0) {
            fprintf(stderr, "%s: socket read error %d\n", appname, r);
            break;
        }
        if (r < sizeof(nbmsg))
            continue;
        if ((ra.sin6_addr.s6_addr[0] != 0xFE) || (ra.sin6_addr.s6_addr[1] != 0x80)) {
            fprintf(stderr, "ignoring non-link-local message\n");
            continue;
        }
        if (msg->magic != NB_MAGIC)
            continue;
        if (msg->cmd != NB_ADVERTISE)
            continue;
        fprintf(stderr, "%s: got beacon from [%s]%d\n", appname,
                inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)),
                ntohs(ra.sin6_port));
        if (cmdline[0]) {
            status = xfer(&ra, "(cmdline)", cmdline, false);
        } else {
            status = 0;
        }
        if ((status == 0) && ramdisk_fn) {
            status = xfer(&ra, ramdisk_fn, "ramdisk.bin", false);
        } else {
            status = 0;
        }
        if (status == 0) {
            xfer(&ra, kernel_fn, "kernel.bin", true);
        }
        if (once) {
            break;
        }
        drain(s);
    }

    return 0;
}
