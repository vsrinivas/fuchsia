// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "eth-client.h"

#include <zircon/syscalls.h>

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if 0
#define IORING_TRACE(fmt...) fprintf(stderr, fmt)
#else
#define IORING_TRACE(fmt...) do {} while (0)
#endif

void eth_destroy(eth_client_t* eth) {
    zx_handle_close(eth->rx_fifo);
    zx_handle_close(eth->tx_fifo);
    free(eth);
}

zx_status_t eth_create(int fd, zx_handle_t io_vmo, void* io_mem, eth_client_t** out) {
    eth_client_t* eth;

    if ((eth = calloc(1, sizeof(*eth))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    eth_fifos_t fifos;
    zx_status_t status;

    ssize_t r;
    if ((r = ioctl_ethernet_get_fifos(fd, &fifos)) < 0) {
        fprintf(stderr, "eth_create: failed to get fifos: %zd\n", r);
        return r;
    }

    zx_handle_t vmo;
    if ((status = zx_handle_duplicate(io_vmo, ZX_RIGHT_SAME_RIGHTS, &vmo)) < 0) {
        fprintf(stderr, "eth_create: failed to duplicate vmo\n");
        goto fail;
    }
    if ((r = ioctl_ethernet_set_iobuf(fd, &vmo)) < 0) {
        fprintf(stderr, "eth_create: failed to set iobuf: %zd\n", r);
        status = r;
        goto fail;
    }
    if ((r = ioctl_ethernet_set_client_name(fd, "netsvc", 6)) < 0) {
        fprintf(stderr, "eth_create: failed to set client name: %zd\n", r);
    }

    eth->tx_fifo = fifos.tx_fifo;
    eth->rx_fifo = fifos.rx_fifo;
    eth->rx_size = fifos.rx_depth;
    eth->tx_size = fifos.tx_depth;
    eth->iobuf = io_mem;

    *out = eth;
    return ZX_OK;

fail:
    zx_handle_close(fifos.tx_fifo);
    zx_handle_close(fifos.rx_fifo);
    eth_destroy(eth);
    return status;
}

zx_status_t eth_queue_tx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options) {
    eth_fifo_entry_t e = {
        .offset = data - eth->iobuf,
        .length = len,
        .flags = options,
        .cookie = cookie,
    };
    uint32_t actual;
    IORING_TRACE("eth:tx+ c=%p o=%u l=%u f=%u\n",
                 e.cookie, e.offset, e.length, e.flags);
    return zx_fifo_write_old(eth->tx_fifo, &e, sizeof(e), &actual);
}

zx_status_t eth_queue_rx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options) {
    eth_fifo_entry_t e = {
        .offset = data - eth->iobuf,
        .length = len,
        .flags = options,
        .cookie = cookie,
    };
    uint32_t actual;
    IORING_TRACE("eth:rx+ c=%p o=%u l=%u f=%u\n",
                 e.cookie, e.offset, e.length, e.flags);
    return zx_fifo_write_old(eth->rx_fifo, &e, sizeof(e), &actual);
}

zx_status_t eth_complete_tx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie)) {
    eth_fifo_entry_t entries[eth->tx_size];
    zx_status_t status;
    uint32_t count;
    if ((status = zx_fifo_read_old(eth->tx_fifo, entries, sizeof(entries), &count)) < 0) {
        if (status == ZX_ERR_SHOULD_WAIT) {
            return ZX_OK;
        } else {
            return status;
        }
    }

    for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
        IORING_TRACE("eth:tx- c=%p o=%u l=%u f=%u\n",
                     e->cookie, e->offset, e->length, e->flags);
        func(ctx, e->cookie);
    }
    return ZX_OK;
}

zx_status_t eth_complete_rx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie, size_t len, uint32_t flags)) {
    eth_fifo_entry_t entries[eth->rx_size];
    zx_status_t status;
    uint32_t count;
    if ((status = zx_fifo_read_old(eth->rx_fifo, entries, sizeof(entries), &count)) < 0) {
        if (status == ZX_ERR_SHOULD_WAIT) {
            return ZX_OK;
        } else {
            return status;
        }
    }

    for (eth_fifo_entry_t* e = entries; count-- > 0; e++) {
        IORING_TRACE("eth:rx- c=%p o=%u l=%u f=%u\n",
                     e->cookie, e->offset, e->length, e->flags);
        func(ctx, e->cookie, e->length, e->flags);
    }
    return ZX_OK;
}


// Wait for completed rx packets
// ZX_ERR_PEER_CLOSED - far side disconnected
// ZX_ERR_TIMED_OUT - deadline lapsed
// ZX_OK - completed packets are available
zx_status_t eth_wait_rx(eth_client_t* eth, zx_time_t deadline) {
    zx_status_t status;
    zx_signals_t signals;

    if ((status = zx_object_wait_one(eth->rx_fifo,
                                     ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                     deadline, &signals)) < 0) {
        if (signals & ZX_FIFO_READABLE) {
            return ZX_OK;
        }
        return status;
    }
    if (signals & ZX_FIFO_PEER_CLOSED) {
        return ZX_ERR_PEER_CLOSED;
    }
    return ZX_OK;
}
