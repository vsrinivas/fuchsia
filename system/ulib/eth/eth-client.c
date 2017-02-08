// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <eth/eth-client.h>
#include <eth/eth-fifo.h>

#include <magenta/syscalls.h>

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if 0
#define IORING_TRACE(fmt...) fprintf(stderr, fmt)
#else
#define IORING_TRACE(fmt...) do {} while (0)
#endif

#define ROUNDUP(a, b) (((a)+ ((b)-1)) & ~((b)-1))

typedef struct eth_client {
    eth_fifo_entry_t* rx_enqueue;
    eth_fifo_entry_t* rx_dequeue;
    mx_handle_t rx_enqueue_fifo;
    mx_handle_t rx_dequeue_fifo;
    uint32_t rx_size;
    uint32_t rx_mask;

    eth_fifo_entry_t* tx_enqueue;
    eth_fifo_entry_t* tx_dequeue;
    mx_handle_t tx_enqueue_fifo;
    mx_handle_t tx_dequeue_fifo;
    uint32_t tx_size;
    uint32_t tx_mask;

    void* iobuf;
} eth_client_t;

void eth_destroy(eth_client_t* eth) {
    mx_handle_close(eth->rx_enqueue_fifo);
    mx_handle_close(eth->rx_dequeue_fifo);
    mx_handle_close(eth->tx_enqueue_fifo);
    mx_handle_close(eth->tx_dequeue_fifo);
    if (eth->rx_enqueue) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)eth->rx_enqueue, 0);
    }
    if (eth->tx_enqueue) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)eth->tx_enqueue, 0);
    }
    memset(eth, 0, sizeof(*eth));
    free(eth);
}

mx_status_t eth_create(int fd, eth_client_args_t* args, eth_client_t** out) {
    eth_client_t* eth;

    if ((eth = calloc(1, sizeof(*eth))) == NULL) {
        return ERR_NO_MEMORY;
    }

    eth_ioring_t ioring;
    mx_status_t status;
    ssize_t r;

    // obtain and map the rx ioring
    if ((r = ioctl_ethernet_get_rx_ioring(fd, &args->rx_entries, &ioring)) < 0) {
        fprintf(stderr, "eth_create: get rx ioring failed: %zd\n", r);
        status = r;
        goto fail;
    }
    eth->rx_enqueue_fifo = ioring.enqueue_fifo;
    eth->rx_dequeue_fifo = ioring.dequeue_fifo;
    eth->rx_size = args->rx_entries;
    eth->rx_mask = args->rx_entries - 1;

    status = mx_vmar_map(mx_vmar_root_self(), 0, ioring.entries_vmo,
                         0, 2 * eth->rx_size * sizeof(eth_fifo_entry_t),
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                         (uintptr_t*) &eth->rx_enqueue);
    mx_handle_close(ioring.entries_vmo);
    if (status < 0) {
        fprintf(stderr, "eth_create: map rx ioring failed: %d\n", status);
        goto fail;
    }
    eth->rx_dequeue = eth->rx_enqueue + eth->rx_size;

    // obtain and map the tx ioring
    if ((r = ioctl_ethernet_get_tx_ioring(fd, &args->tx_entries, &ioring)) < 0) {
        fprintf(stderr, "eth_create: get tx ioring failed: %zd\n", r);
        status = r;
        goto fail;
    }
    eth->tx_enqueue_fifo = ioring.enqueue_fifo;
    eth->tx_dequeue_fifo = ioring.dequeue_fifo;
    eth->tx_size = args->tx_entries;
    eth->tx_mask = args->tx_entries - 1;

    status = mx_vmar_map(mx_vmar_root_self(), 0, ioring.entries_vmo,
                         0, 2 * eth->tx_size * sizeof(eth_fifo_entry_t),
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                         (uintptr_t*) &eth->tx_enqueue);
    mx_handle_close(ioring.entries_vmo);
    if (status < 0) {
        fprintf(stderr, "eth_create: map tx ioring failed: %d\n", status);
        goto fail;
    }
    eth->tx_dequeue = eth->tx_enqueue + eth->tx_size;

    // attach io buffer vmo to driver
    mx_handle_t vmo;
    if ((status = mx_handle_duplicate(args->iobuf_vmo, MX_RIGHT_SAME_RIGHTS, &vmo)) < 0) {
        goto fail;
    }
    if ((status = ioctl_ethernet_set_iobuf(fd, &vmo)) < 0) {
        fprintf(stderr, "eth_create: set iobuf failed: %d\n", status);
        goto fail;
    }
    eth->iobuf = args->iobuf;

    *out = eth;
    return NO_ERROR;

fail:
    eth_destroy(eth);
    return status;
}

mx_status_t eth_queue_tx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options) {
    mx_status_t status;
    mx_fifo_state_t state;

    if ((status = mx_fifo0_op(eth->tx_enqueue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        return status;
    }

    if ((state.head - state.tail) < eth->tx_size) {
        unsigned idx = state.head & eth->tx_mask;
        eth_fifo_entry_t* entry = eth->tx_enqueue + idx;
        entry->offset = data - eth->iobuf;
        entry->length = len;
        entry->flags = options;
        entry->cookie = cookie;
        IORING_TRACE("tx[%u] c=%p o=%u l=%u f=%u\n",
                     (unsigned)idx, entry->cookie, entry->offset, entry->length, entry->flags);
        return mx_fifo0_op(eth->tx_enqueue_fifo, MX_FIFO_OP_ADVANCE_HEAD, 1u, &state);
    } else {
        return ERR_SHOULD_WAIT;
    }
}

mx_status_t eth_queue_rx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options) {
    mx_status_t status;
    mx_fifo_state_t state;

    if ((status = mx_fifo0_op(eth->rx_enqueue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        return status;
    }

    if ((state.head - state.tail) < eth->rx_size) {
        unsigned idx = state.head & eth->rx_mask;
        eth_fifo_entry_t* entry = eth->rx_enqueue + idx;
        entry->offset = data - eth->iobuf;
        entry->length = len;
        entry->flags = options;
        entry->cookie = cookie;
        IORING_TRACE("rx[%u] c=%p o=%u l=%u f=%u\n",
                     (unsigned)idx, entry->cookie, entry->offset, entry->length, entry->flags);
        return mx_fifo0_op(eth->rx_enqueue_fifo, MX_FIFO_OP_ADVANCE_HEAD, 1u, &state);
    } else {
        return ERR_SHOULD_WAIT;
    }
}

mx_status_t eth_complete_tx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie)) {
    mx_status_t status;
    mx_fifo_state_t state;

    if ((status = mx_fifo0_op(eth->tx_dequeue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        return status;
    }

    unsigned count = 0;
    while ((state.head - state.tail) > 0) {
        unsigned idx = state.tail++ & eth->tx_mask;
        eth_fifo_entry_t* entry = eth->tx_dequeue + idx;
        IORING_TRACE("TX[%u] c=%p o=%u l=%u f=%u\n",
                     (unsigned)idx, entry->cookie, entry->offset, entry->length, entry->flags);
        func(ctx, entry->cookie);
        count++;
    }

    return mx_fifo0_op(eth->tx_dequeue_fifo, MX_FIFO_OP_ADVANCE_TAIL, count, NULL);
}


// Process all transmitted buffers
mx_status_t eth_complete_rx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie, size_t len, uint32_t flags)) {

    mx_status_t status;
    mx_fifo_state_t state;

    if ((status = mx_fifo0_op(eth->rx_dequeue_fifo, MX_FIFO_OP_READ_STATE, 0, &state)) < 0) {
        return status;
    }

    unsigned count = 0;
    while ((state.head - state.tail) > 0) {
        unsigned idx = state.tail++ & eth->rx_mask;
        eth_fifo_entry_t* entry = eth->rx_dequeue + idx;
        IORING_TRACE("RX[%u] c=%p o=%u l=%u f=%u\n",
                     (unsigned)idx, entry->cookie, entry->offset, entry->length, entry->flags);
        func(ctx, entry->cookie, entry->length, entry->flags);
        count++;
    }

    return mx_fifo0_op(eth->rx_dequeue_fifo, MX_FIFO_OP_ADVANCE_TAIL, count, NULL);
}

// Wait for completed rx packets
// ERR_REMOTE_CLOSED - far side disconnected
// ERR_TIMED_OUT - timeout expired
// NO_ERROR - completed packets are available
mx_status_t eth_wait_rx(eth_client_t* eth, mx_time_t timeout) {
    mx_status_t status;
    mx_signals_t signals;

    if ((status = mx_handle_wait_one(eth->rx_dequeue_fifo,
                                     MX_FIFO_NOT_EMPTY | MX_FIFO_PRODUCER_EXCEPTION,
                                     timeout, &signals)) < 0) {
        return status;
    }
    if (signals & MX_FIFO_PRODUCER_EXCEPTION) {
        return ERR_REMOTE_CLOSED;
    }
    return NO_ERROR;
}
