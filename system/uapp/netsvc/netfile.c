// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <launchpad/launchpad.h>

#include <magenta/netboot.h>

netfile_state netfile;

void netfile_open(const char *filename, uint32_t cookie, uint32_t arg,
                  const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = 0;

    if (netfile.fd > 0) {
        printf("netsvc: closing still-open %s, replacing with %s\n", netfile.filename, filename);
        close(netfile.fd);
        memset(&netfile, 0, sizeof(netfile));
    }
    netfile.blocknum = 0;

    switch (arg) {
    case O_RDONLY:
        netfile.fd = open(filename, O_RDONLY);
        break;
    case O_WRONLY:
        netfile.fd = open(filename, O_WRONLY|O_CREAT);
        break;
    default:
        printf("netsvc: open %s with invalid mode %d\n", filename, arg);
        m.arg = -EINVAL;
    }
    if (netfile.fd == -1) {
        m.arg = -errno;
        if (m.arg == 0) {
            m.arg = -EPERM;
        }
        udp6_send(&m, sizeof(m), saddr, sport, dport);
        return;
    } else {
        strlcpy(netfile.filename, filename, sizeof(netfile.filename));
    }

    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

void netfile_read(uint32_t cookie, uint32_t arg,
                  const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    netfilemsg m;
    m.hdr.magic = NB_MAGIC;
    m.hdr.cookie = cookie;
    m.hdr.cmd = NB_ACK;
    m.hdr.arg = 0;

    if (!netfile.fd) {
        printf("netsvc: read, but no open file\n");
        m.hdr.arg = -EBADF;
        udp6_send(&m.hdr, sizeof(m.hdr), saddr, sport, dport);
        return;
    }
    if (arg != netfile.blocknum) {
        printf("netsvc: read blocknum is %d, want %d\n", arg, netfile.blocknum);
        m.hdr.arg = -EPERM;
        udp6_send(&m.hdr, sizeof(m.hdr), saddr, sport, dport);
        return;
    }
    netfile.blocknum++;

    ssize_t n = read(netfile.fd, m.data, sizeof(m.data));
    if (n < 0) {
        n = 0;
        printf("netsvc: error reading %s: %d\n", netfile.filename, errno);
        m.hdr.arg = -errno;
        if (m.hdr.arg == 0) {
            m.hdr.arg = -EIO;
        }
        close(netfile.fd);
        memset(&netfile, 0, sizeof(netfile));
    }

    udp6_send(&m, sizeof(m.hdr) + n, saddr, sport, dport);
}

void netfile_write(const char* data, size_t len, uint32_t cookie, uint32_t arg,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    printf("netsvc: netfile_write TODO\n");
}

void netfile_close(uint32_t cookie,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = 0;

    if (!netfile.fd) {
        printf("netsvc: close, but no open file\n");
        m.arg = -EBADF;
    } else {
        if (close(netfile.fd)) {
            m.arg = -errno;
            if (m.arg == 0) {
                m.arg = -EIO;
            }
        }
        memset(&netfile, 0, sizeof(netfile));
    }
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}
