// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include "netprotocol.h"

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

#include <magenta/netboot.h>

static const char* appname;

static int pull_file(int s, const char* dst, const char* src) {
    int r;
    msg in, out;
    size_t src_len = strlen(src);

    out.hdr.cmd = NB_OPEN;
    out.hdr.arg = O_RDONLY;
    memcpy(out.data, src, src_len);
    out.data[src_len] = 0;

    r = netboot_txn(s, &in, &out, sizeof(out.hdr) + src_len + 1);
    if (r < 0) {
        fprintf(stderr, "%s: error opening remote file %s (%d)\n",
                appname, src, errno);
        return r;
    }

    int fd = open(dst, O_WRONLY|O_TRUNC|O_CREAT, 0664);
    if (!fd) {
        fprintf(stderr, "%s: cannot open %s for writing: %s\n",
                appname, dst, strerror(errno));
        return -1;
    }

    int n = 0;
    int blocknum = 0;
    for (;;) {
        memset(&out, 0, sizeof(out));
        out.hdr.cmd = NB_READ;
        out.hdr.arg = blocknum;
        r = netboot_txn(s, &in, &out, sizeof(out.hdr) + 1);
        if (r < 0) {
            fprintf(stderr, "%s: error reading block %d (%d)\n",
                    appname, blocknum, errno);
            close(fd);
            return r;
        }
        r -= sizeof(in.hdr);
        if (r == 0) {
            break; // EOF
        }
        if (write(fd, in.data, r) < r) {
            fprintf(stderr, "%s: pull short local write: %s\n",
                    appname, strerror(errno));
            close(fd);
            return -1;
        }
        blocknum++;
        n += r;
    }

    memset(&out, 0, sizeof(out));
    out.hdr.cmd = NB_CLOSE;
    r = netboot_txn(s, &in, &out, sizeof(out.hdr) + 1);
    if (r < 0) {
        close(fd);
        return r;
    }

    if (close(fd)) {
        fprintf(stderr, "%s: pull local close failed: %s\n",
                appname, strerror(errno));
        return -1;
    }

    fprintf(stderr, "read %d bytes\n", n);

    return 0;
}

static int push_file(int s, const char* dst, const char* src) {
    fprintf(stderr, "push_file TODO");
    return -1;
}

int main(int argc, char** argv) {
    appname = argv[0];

    if (argc != 3) {
        fprintf(stderr, "usage: %s [hostname:]src [hostname:]dst\n", appname);
        return -1;
    }

    const char* src = argv[1];
    const char* dst = argv[2];

    int push = -1;
    char* pos;
    const char* hostname;
    if ((pos = strpbrk(src, ":")) != 0) {
        push = 0;
        hostname = src;
        pos[0] = 0;
        src = pos+1;
    }
    if ((pos = strpbrk(dst, ":")) != 0) {
        if (push == 0) {
            fprintf(stderr, "%s: only one of src or dst can have a hostname\n", appname);
            return -1;
        }
        push = 1;
        hostname = dst;
        pos[0] = 0;
        dst = pos+1;
    }
    if (push == -1) {
        fprintf(stderr, "%s: either src or dst needs a hostname\n", appname);
        return -1;
    }

    int s;
    if ((s = netboot_open(hostname, NB_SERVER_PORT, NULL)) < 0) {
        if (errno == ETIMEDOUT) {
            fprintf(stderr, "%s: lookup of %s timed out\n", appname, hostname);
        } else {
            fprintf(stderr, "%s: failed to connect to %s: %d\n", appname, hostname, errno);
        }
        return -1;
    }

    int ret;
    if (push) {
        ret = push_file(s, dst, src);
    } else {
        ret = pull_file(s, dst, src);
    }
    close(s);
    return ret;
}
