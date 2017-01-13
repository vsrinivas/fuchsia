// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <eth/eth-fifo.h>

#include <magenta/syscalls.h>

#include <limits.h>
#include <stdio.h>

// borrowed from LK/magenta stdlib.h
#define ROUNDUP(a, b) (((a)+ ((b)-1)) & ~((b)-1))
#define ALIGN(a, b) ROUNDUP(a, b)

mx_status_t eth_fifo_create(uint32_t rx_entries, uint32_t tx_entries, uint32_t options,
        eth_fifo_t* out) {
    // No options supported yet.
    if (options != 0) {
        return ERR_INVALID_ARGS;
    }

    size_t rx_entry_size = ALIGN(sizeof(eth_fifo_entry_t) * rx_entries, PAGE_SIZE);
    size_t tx_entry_size = ALIGN(sizeof(eth_fifo_entry_t) * tx_entries, PAGE_SIZE);
    uint64_t fifo_vmo_size = rx_entry_size + tx_entry_size;
    mx_status_t status = mx_vmo_create(fifo_vmo_size, 0, &out->entries_vmo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }

    status = mx_fifo_create(rx_entries, &out->rx_fifo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }

    status = mx_fifo_create(tx_entries, &out->tx_fifo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }

    out->version = 1;
    out->options = options;
    out->rx_entries_count = rx_entries;
    out->tx_entries_count = tx_entries;

    return NO_ERROR;
}

mx_status_t eth_fifo_clone_consumer(eth_fifo_t* in, eth_fifo_t* out) {
    if (in == out) {
        printf("eth_fifo: clone to self not supported\n");
        return ERR_NOT_SUPPORTED;
    }

    // Clone the fifo VMO handle
    mx_status_t status = mx_handle_duplicate(in->entries_vmo, MX_RIGHT_SAME_RIGHTS, &out->entries_vmo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }
    // Drop producer rights
    status = mx_handle_duplicate(in->rx_fifo, MX_FIFO_CONSUMER_RIGHTS, &out->rx_fifo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }
    status = mx_handle_duplicate(in->tx_fifo, MX_FIFO_CONSUMER_RIGHTS, &out->tx_fifo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }

    out->version = in->version;
    out->options = in->options;
    out->rx_entries_count = in->rx_entries_count;
    out->tx_entries_count = in->tx_entries_count;

    return NO_ERROR;
}

mx_status_t eth_fifo_clone_producer(eth_fifo_t* in, eth_fifo_t* out) {
    if (in == out) {
        printf("eth_fifo: clone to self not supported\n");
        return ERR_NOT_SUPPORTED;
    }

    // Clone the fifo VMO handle
    mx_status_t status = mx_handle_duplicate(in->entries_vmo, MX_RIGHT_SAME_RIGHTS, &out->entries_vmo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }
    // Drop consumer rights
    status = mx_handle_duplicate(in->rx_fifo, MX_FIFO_PRODUCER_RIGHTS, &out->rx_fifo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }
    status = mx_handle_duplicate(in->tx_fifo, MX_FIFO_PRODUCER_RIGHTS, &out->tx_fifo);
    if (status != NO_ERROR) {
        eth_fifo_cleanup(out);
        return status;
    }

    out->version = in->version;
    out->options = in->options;
    out->rx_entries_count = in->rx_entries_count;
    out->tx_entries_count = in->tx_entries_count;

    return NO_ERROR;
}

void eth_fifo_cleanup(eth_fifo_t* fifo) {
    if (fifo->entries_vmo != MX_HANDLE_INVALID) {
        mx_handle_close(fifo->entries_vmo);
        fifo->entries_vmo = MX_HANDLE_INVALID;
    }
    if (fifo->rx_fifo != MX_HANDLE_INVALID) {
        mx_handle_close(fifo->rx_fifo);
        fifo->rx_fifo = MX_HANDLE_INVALID;
    }
    if (fifo->tx_fifo != MX_HANDLE_INVALID) {
        mx_handle_close(fifo->tx_fifo);
        fifo->tx_fifo = MX_HANDLE_INVALID;
    }
    fifo->version = 0;
    fifo->options = 0;
    fifo->rx_entries_count = 0;
    fifo->tx_entries_count = 0;
}

mx_status_t eth_fifo_map_rx_entries(eth_fifo_t* fifo, void* addr) {
    size_t rx_entry_size = ALIGN(sizeof(eth_fifo_entry_t) * fifo->rx_entries_count, PAGE_SIZE);
    return mx_vmar_map(mx_vmar_root_self(), 0, fifo->entries_vmo, 0,
            rx_entry_size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)addr);
}

mx_status_t eth_fifo_map_tx_entries(eth_fifo_t* fifo, void* addr) {
    size_t rx_entry_size = ALIGN(sizeof(eth_fifo_entry_t) * fifo->rx_entries_count, PAGE_SIZE);
    size_t tx_entry_size = ALIGN(sizeof(eth_fifo_entry_t) * fifo->tx_entries_count, PAGE_SIZE);
    return mx_vmar_map(mx_vmar_root_self(), 0, fifo->entries_vmo, rx_entry_size,
            tx_entry_size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)addr);
}
