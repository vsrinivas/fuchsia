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
#include <sys/stat.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <launchpad/launchpad.h>

#include <magenta/netboot.h>

netfile_state netfile = {
    .fd = -1,
};

static int netfile_mkdir(const char* filename) {
    const char* ptr = filename[0] == '/' ? filename + 1 : filename;
    struct stat st;
    char tmp[1024];
    for (;;) {
        ptr = strchr(ptr, '/');
        if (!ptr) {
            return 0;
        }
        memcpy(tmp, filename, ptr - filename);
        tmp[ptr - filename] = '\0';
        ptr += 1;
        if (stat(tmp, &st) < 0) {
            if (errno == ENOENT) {
                if (mkdir(tmp, 0755) < 0) {
                    return -1;
                }
            } else {
                return -1;
            }
        }
    }
}

void netfile_open(const char *filename, uint32_t cookie, uint32_t arg,
                  const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = 0;

    if (netfile.fd >= 0) {
        printf("netsvc: closing still-open '%s', replacing with '%s'\n", netfile.filename, filename);
        close(netfile.fd);
        netfile.fd = -1;
    }
    netfile.blocknum = 0;
    netfile.cookie = cookie;

    struct stat st;
again: // label here to catch filename=/path/to/new/directory/
    if (stat(filename, &st) == 0 && S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        goto err;
    }

    switch (arg) {
    case O_RDONLY:
        netfile.fd = open(filename, O_RDONLY);
        break;
    case O_WRONLY: {
        netfile.fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC);
        if (netfile.fd < 0 && errno == ENOENT) {
            if (netfile_mkdir(filename) == 0) {
                goto again;
            }
        }
        break;
    }
    default:
        printf("netsvc: open '%s' with invalid mode %d\n", filename, arg);
        errno = EINVAL;
    }
    if (netfile.fd < 0) {
        goto err;
    } else {
        strlcpy(netfile.filename, filename, sizeof(netfile.filename));
    }

    udp6_send(&m, sizeof(m), saddr, sport, dport);
    return;
err:
    m.arg = -errno;
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

void netfile_read(uint32_t cookie, uint32_t arg,
                  const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    netfilemsg m;
    m.hdr.magic = NB_MAGIC;
    m.hdr.cookie = cookie;
    m.hdr.cmd = NB_ACK;

    if (netfile.fd < 0) {
        printf("netsvc: read, but no open file\n");
        m.hdr.arg = -EBADF;
        udp6_send(&m.hdr, sizeof(m.hdr), saddr, sport, dport);
        return;
    }
    if (arg == (netfile.blocknum - 1)) {
        // repeat of last block read, probably due to dropped packet
        // unless cookie doesn't match, in which case it's an error
        if (cookie != netfile.cookie) {
            m.hdr.arg = -EIO;
            udp6_send(&m.hdr, sizeof(m.hdr), saddr, sport, dport);
            return;
        }
    } else if (arg != netfile.blocknum) {
        // ignore bogus read requests -- host will timeout if they're confused
        return;
    } else {
        ssize_t n = read(netfile.fd, netfile.data, sizeof(netfile.data));
        if (n < 0) {
            n = 0;
            printf("netsvc: error reading '%s': %d\n", netfile.filename, errno);
            m.hdr.arg = -errno;
            close(netfile.fd);
            netfile.fd = -1;
            udp6_send(&m.hdr, sizeof(m.hdr), saddr, sport, dport);
            return;
        }
        netfile.datasize = n;
        netfile.blocknum++;
        netfile.cookie = cookie;
    }

    m.hdr.arg = arg;
    memcpy(m.data, netfile.data, netfile.datasize);
    udp6_send(&m, sizeof(m.hdr) + netfile.datasize, saddr, sport, dport);
}

void netfile_write(const char* data, size_t len, uint32_t cookie, uint32_t arg,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = 0;

    if (netfile.fd < 0) {
        printf("netsvc: write, but no open file\n");
        m.arg = -EBADF;
        udp6_send(&m, sizeof(m), saddr, sport, dport);
        return;
    }

    if (arg == (netfile.blocknum - 1)) {
        // repeat of last block write, probably due to dropped packet
        // unless cookie doesn't match, in which case it's an error
        if (cookie != netfile.cookie) {
            m.arg = -EIO;
            udp6_send(&m, sizeof(m), saddr, sport, dport);
            return;
        }
    } else if (arg != netfile.blocknum) {
        // ignore bogus write requests -- host will timeout if they're confused
        return;
    } else {
        ssize_t n = write(netfile.fd, data, len);
        if (n != (ssize_t)len) {
            printf("netsvc: error writing %s: %d\n", netfile.filename, errno);
            m.arg = -errno;
            if (m.arg == 0) {
                m.arg = -EIO;
            }
            close(netfile.fd);
            netfile.fd = -1;
            udp6_send(&m, sizeof(m), saddr, sport, dport);
            return;
        }
        netfile.blocknum++;
        netfile.cookie = cookie;
    }

    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

void netfile_close(uint32_t cookie,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = 0;

    if (netfile.fd < 0) {
        printf("netsvc: close, but no open file\n");
    } else {
        if (close(netfile.fd)) {
            m.arg = -errno;
            if (m.arg == 0) {
                m.arg = -EIO;
            }
        }
        netfile.fd = -1;
    }
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}
