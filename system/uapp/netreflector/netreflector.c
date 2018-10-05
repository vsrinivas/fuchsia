// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inet6/inet6.h>
#include <lib/fdio/util.h>
#include <zircon/ethernet/c/fidl.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define SRC_PORT 5004
#define DST_PORT 5005

#define BUFSIZE 2048
#define BUFS 256

#define RX_FIFO 0
#define TX_FIFO 1

typedef struct eth_buf eth_buf_t;

struct eth_buf {
    eth_buf_t* next;
    zircon_ethernet_FifoEntry* e;
};

typedef struct {
    mac_addr_t dst;
    mac_addr_t src;
    uint16_t type;
} __PACKED eth_hdr_t;

eth_buf_t* avail_tx_buffers = NULL;
eth_buf_t* pending_tx = NULL;
zx_handle_t port = ZX_HANDLE_INVALID;

void flip_src_dst(void* packet) {
    eth_hdr_t* eth = packet;
    mac_addr_t src_mac = eth->src;
    eth->src = eth->dst;
    eth->dst = src_mac;

    ip6_hdr_t* ip = packet + ETH_HDR_LEN;
    ip6_addr_t src_ip = ip->src;
    ip->src = ip->dst;
    ip->dst = src_ip;
    ip->next_header = HDR_UDP;

    udp_hdr_t* udp = packet + ETH_HDR_LEN + IP6_HDR_LEN;
    uint16_t src_port = udp->src_port;
    udp->src_port = udp->dst_port;
    udp->dst_port = src_port;
    udp->checksum = 0;
    udp->checksum = ip6_checksum(ip, HDR_UDP, ntohs(ip->length));
}

void send_pending_tx(zx_handle_t tx_fifo) {
    zx_status_t status;
    while (pending_tx != NULL) {
        zircon_ethernet_FifoEntry* e = pending_tx->e;
        e->cookie = (uint64_t)pending_tx;
        if ((status = zx_fifo_write(tx_fifo, sizeof(*e), e, 1, NULL)) != ZX_OK) {
            fprintf(stderr, "netreflector: error reflecting packet %d\n", status);
            return;
        }
        pending_tx = pending_tx->next;
    }
}

void tx_complete(zircon_ethernet_FifoEntry* e) {
    if (e->flags & zircon_ethernet_FIFO_TX_OK) {
        eth_buf_t* buf = (eth_buf_t*)e->cookie;
        buf->next = avail_tx_buffers;
        avail_tx_buffers = buf;
    }
}

zx_status_t acquire_tx_buffer(eth_buf_t** out) {
    if (avail_tx_buffers == NULL) {
        fprintf(stderr, "netreflector: no tx buffers available.\n");
        return ZX_ERR_SHOULD_WAIT;
    }
    *out = avail_tx_buffers;
    avail_tx_buffers = avail_tx_buffers->next;
    return ZX_OK;
}

void queue_tx_buffer(eth_buf_t* tx) {
    tx->next = pending_tx;
    pending_tx = tx;
}

zx_status_t reflect_packet(char* iobuf, zircon_ethernet_FifoEntry* e) {
    eth_buf_t* tx;
    zx_status_t status;
    if ((status = acquire_tx_buffer(&tx)) != ZX_OK) {
        return status;
    }
    tx->e->length = e->length;

    void* in_pkt = iobuf + e->offset;
    void* out_pkt = iobuf + tx->e->offset;
    memcpy(out_pkt, in_pkt, tx->e->length);
    flip_src_dst(out_pkt);

    queue_tx_buffer(tx);
    return ZX_OK;
}

void rx_complete(char* iobuf, zx_handle_t rx_fifo, zircon_ethernet_FifoEntry* e) {
    if (!(e->flags & zircon_ethernet_FIFO_RX_OK)) {
        return;
    }
    if (e->length < ETH_HDR_LEN + IP6_HDR_LEN + UDP_HDR_LEN) {
        goto queue;
    }

    // Only reflect packets arriving from DST_PORT on SRC_PORT.
    void* in_pkt = iobuf + e->offset;
    udp_hdr_t* udp = in_pkt + ETH_HDR_LEN + IP6_HDR_LEN;
    if (ntohs(udp->dst_port) != DST_PORT || ntohs(udp->src_port) != SRC_PORT) {
        goto queue;
    }
    reflect_packet(iobuf, e);

queue:
    e->length = BUFSIZE;
    e->flags = 0;
    zx_status_t status;
    if ((status = zx_fifo_write(rx_fifo, sizeof(*e), e, 1, NULL)) != ZX_OK) {
        fprintf(stderr, "netreflector: failed to queue rx packet: %d\n", status);
    }
}

void handle(char* iobuf, zircon_ethernet_Fifos* fifos) {
    zx_port_packet_t packet;
    zx_status_t status;
    size_t n;
    zircon_ethernet_FifoEntry entries[BUFS];
    for (;;) {
        status = zx_port_wait(port, ZX_TIME_INFINITE, &packet);
        if (status != ZX_OK) {
            fprintf(stderr, "netreflector: error while waiting on port %d\n", status);
            return;
        }

        if (packet.signal.observed & ZX_FIFO_PEER_CLOSED) {
            fprintf(stderr, "netreflector: fifo closed\n");
            return;
        }

        if (packet.signal.observed & ZX_FIFO_READABLE) {
            uint8_t fifo_id = (uint8_t)packet.key;
            zx_handle_t fifo = (fifo_id == RX_FIFO ? fifos->rx: fifos->tx);
            if ((status = zx_fifo_read(fifo, sizeof(entries[0]), entries, countof(entries), &n)) != ZX_OK) {
                fprintf(stderr, "netreflector: error reading fifo %d\n", status);
                continue;
            }

            zircon_ethernet_FifoEntry* e = entries;
            switch (fifo_id) {
            case TX_FIFO:
                for (size_t i = 0; i < n; i++, e++) {
                    tx_complete(e);
                }
                break;
            case RX_FIFO:
                for (size_t i = 0; i < n; i++, e++) {
                    rx_complete(iobuf, fifos->rx, e);
                }
                break;
            default:
                fprintf(stderr, "netreflector: unknown key %lu\n", packet.key);
                break;
            }
        }
        send_pending_tx(fifos->tx);
    }
}

int main(int argc, char** argv) {
    const char* ethdev = (argc > 1 ? argv[1] : "/dev/class/ethernet/000");
    int fd;
    if ((fd = open(ethdev, O_RDWR)) < 0) {
        fprintf(stderr, "netreflector: cannot open '%s'\n", argv[1]);
        return -1;
    }

    zx_handle_t svc;
    zx_status_t status = fdio_get_service_handle(fd, &svc);
    if (status != ZX_OK) {
        fprintf(stderr, "netreflector: couldn't convert to handle\n");
        return -1;
    }

    zircon_ethernet_Fifos fifos;

    zx_status_t call_status = ZX_OK;
    status = zircon_ethernet_DeviceSetClientName(svc, "netreflector", 13, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "netreflector: failed to set client name %d, %d\n", status, call_status);
    }

    status = zircon_ethernet_DeviceGetFifos(svc, &call_status, &fifos);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "netreflector: failed to set client name %d, %d\n", status, call_status);
        return -1;
    }

    // Allocate shareable ethernet buffer data heap. The first BUFS entries represent rx buffers,
    // followed by BUFS entries representing tx buffers.
    unsigned count = BUFS * 2;
    zx_handle_t iovmo;
    if ((status = zx_vmo_create(count * BUFSIZE, ZX_VMO_NON_RESIZABLE, &iovmo)) < 0) {
        return -1;
    }
    char* iobuf;
    if ((status = zx_vmar_map(zx_vmar_root_self(),
                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                              0, iovmo, 0, count * BUFSIZE, (uintptr_t*)&iobuf)) < 0) {
        return -1;
    }

    status = zircon_ethernet_DeviceSetIOBuffer(svc, iovmo, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "netreflector: failed to set iobuf: %d, %d\n", status, call_status);
        return -1;
    }

    // Write first BUFS entries to rx fifo...
    unsigned n = 0;
    for (; n < BUFS; n++) {
        zircon_ethernet_FifoEntry entry = {
            .offset = n * BUFSIZE, .length = BUFSIZE, .flags = 0, .cookie = 0,
        };
        if ((status = zx_fifo_write(fifos.rx, sizeof(entry), &entry, 1, NULL)) < 0) {
            fprintf(stderr, "netreflector: failed to queue rx packet: %d\n", status);
            return -1;
        }
    }

    // ... continue writing next BUFS entries to tx fifo.
    eth_buf_t* buf = malloc(sizeof(eth_buf_t) * BUFS);
    for (; n < count; n++, buf++) {
        zircon_ethernet_FifoEntry entry = {
            .offset = n * BUFSIZE, .length = BUFSIZE, .flags = 0,
            .cookie = (uint64_t)buf,
        };
        buf->e = &entry;
        buf->next = avail_tx_buffers;
        avail_tx_buffers = buf;
    }

    status = zircon_ethernet_DeviceStart(svc, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "netreflector: failed to start network interface\n");
        return -1;
    }

    if (zx_port_create(0, &port) != ZX_OK) {
        fprintf(stderr, "netreflector: failed to create port\n");
        return -1;
    }

    uint32_t signals = ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED;
    if ((status = zx_object_wait_async(fifos.rx, port, RX_FIFO, signals,
                                       ZX_WAIT_ASYNC_REPEATING)) != ZX_OK) {
        fprintf(stderr, "netreflector: failed binding port to rx fifo %d\n", status);
        return -1;
    }

    if ((status = zx_object_wait_async(fifos.tx, port, TX_FIFO, signals,
                                       ZX_WAIT_ASYNC_REPEATING)) != ZX_OK) {
        fprintf(stderr, "netreflector: failed binding port to tx fifo %d\n", status);
        return -1;
    }

    handle(iobuf, &fifos);

    return 0;
}
