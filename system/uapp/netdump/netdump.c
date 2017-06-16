// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/ethernet.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <pretty/hexdump.h>

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BUFSIZE 2048

void handle_rx(mx_handle_t rx_fifo, char* iobuf, unsigned count) {
    eth_fifo_entry_t entries[count];

    for (;;) {
        uint32_t n;
        mx_status_t status;
        if ((status = mx_fifo_read(rx_fifo, entries, sizeof(entries), &n)) < 0) {
            if (status == MX_ERR_SHOULD_WAIT) {
                mx_object_wait_one(rx_fifo, MX_FIFO_READABLE | MX_FIFO_PEER_CLOSED, MX_TIME_INFINITE, NULL);
                continue;
            }
            fprintf(stderr, "netdump: failed to read rx packets: %d\n", status);
            return;
        }

        eth_fifo_entry_t* e = entries;
        for (uint32_t i = 0; i < n; i++, e++) {
            if (e->flags & ETH_FIFO_RX_OK) {
                printf("---\n");
                hexdump8_ex(iobuf + e->offset, e->length, 0);
            }
            e->length = BUFSIZE;
            e->flags = 0;
            uint32_t actual;
            if ((status = mx_fifo_write(rx_fifo, e, sizeof(*e), &actual)) < 0) {
                fprintf(stderr, "netdump: failed to queue rx packet: %d\n", status);
                break;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage:  netdump <network-device>\n");
        return -1;
    }

    int fd;
    if ((fd = open(argv[1], O_RDWR)) < 0) {
        fprintf(stderr, "netdump: cannot open '%s'\n", argv[1]);
        return -1;
    }

    eth_fifos_t fifos;
    mx_status_t status;

    ssize_t r;
    if ((r = ioctl_ethernet_get_fifos(fd, &fifos)) < 0) {
        fprintf(stderr, "netdump: failed to get fifos: %zd\n", r);
        return r;
    }

    unsigned count = fifos.rx_depth / 2;
    mx_handle_t iovmo;
    // allocate shareable ethernet buffer data heap
    if ((status = mx_vmo_create(count * BUFSIZE, 0, &iovmo)) < 0) {
        return -1;
    }

    char* iobuf;
    if ((status = mx_vmar_map(mx_vmar_root_self(), 0, iovmo, 0, count * BUFSIZE,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                              (uintptr_t*)&iobuf)) < 0) {
        return -1;
    }

    if ((r = ioctl_ethernet_set_iobuf(fd, &iovmo)) < 0) {
        fprintf(stderr, "netdump: failed to set iobuf: %zd\n", r);
        return -1;
    }

    if ((r = ioctl_ethernet_set_client_name(fd, "netdump", 7)) < 0) {
        fprintf(stderr, "netdump: failed to set client name %zd\n", r);
    }

    // assign data chunks to ethbufs
    for (unsigned n = 0; n < count; n++) {
        eth_fifo_entry_t entry = {
            .offset = n * BUFSIZE,
            .length = BUFSIZE,
            .flags = 0,
            .cookie = NULL,
        };
        uint32_t actual;
        if ((status = mx_fifo_write(fifos.rx_fifo, &entry, sizeof(entry), &actual)) < 0) {
            fprintf(stderr, "netdump: failed to queue rx packet: %d\n", status);
            return -1;
        }
    }

    if (ioctl_ethernet_start(fd) < 0) {
        fprintf(stderr, "netdump: failed to start network interface\n");
        return -1;
    }

    if (ioctl_ethernet_tx_listen_start(fd) < 0) {
        fprintf(stderr, "netdump: failed to start listening\n");
        return -1;
    }

    handle_rx(fifos.rx_fifo, iobuf, count);

    return 0;
}
