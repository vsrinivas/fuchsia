// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <eth/eth-fifo.h>

#include <magenta/syscalls.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

// borrowed from LK/magenta stdlib.h
#define ROUNDUP(a, b) (((a)+ ((b)-1)) & ~((b)-1))
#define ALIGN(a, b) ROUNDUP(a, b)

void eth_ioring_destroy(eth_ioring_t* ioring) {
    if (ioring->entries_vmo) {
        mx_handle_close(ioring->entries_vmo);
        ioring->entries_vmo = 0;
    }
    if (ioring->enqueue_fifo) {
        mx_handle_close(ioring->enqueue_fifo);
        ioring->enqueue_fifo = 0;
    }
    if (ioring->dequeue_fifo) {
        mx_handle_close(ioring->dequeue_fifo);
        ioring->dequeue_fifo = 0;
    }
}

mx_status_t eth_ioring_create(size_t entries, size_t entry_size,
                              eth_ioring_t* cli, eth_ioring_t* srv) {
    if ((entries > 8192) || (entry_size > 256)) {
        return ERR_INVALID_ARGS;
    }

    memset(cli, 0, sizeof(*cli));
    memset(srv, 0, sizeof(*srv));
    mx_handle_t fifo0 = 0;
    mx_handle_t fifo1 = 0;

    mx_status_t status;
    if ((status = mx_fifo0_create(entries, &fifo0)) < 0) {
        return status;
    }
    if ((status = mx_fifo0_create(entries, &fifo1)) < 0) {
        goto fail;
    }

    // clients are producers of "enqueue" and consumers of "dequeue"
    if ((status = mx_handle_duplicate(fifo0, MX_FIFO_PRODUCER_RIGHTS, &cli->enqueue_fifo)) < 0) {
        goto fail;
    }
    if ((status = mx_handle_duplicate(fifo1, MX_FIFO_CONSUMER_RIGHTS, &cli->dequeue_fifo)) < 0) {
        goto fail;
    }

    // servers are consuemrs of "enqueue" and producers of "dequeue"
    if ((status = mx_handle_duplicate(fifo0, MX_FIFO_CONSUMER_RIGHTS, &srv->enqueue_fifo)) < 0) {
        goto fail;
    }
    if ((status = mx_handle_duplicate(fifo1, MX_FIFO_PRODUCER_RIGHTS, &srv->dequeue_fifo)) < 0) {
        goto fail;
    }

    // both share a vmo with a set of enqueue and set of dequeue entries
    if ((status = mx_vmo_create(2 * entries * entry_size, 0, &cli->entries_vmo)) < 0) {
        goto fail;
    }
    if ((status = mx_handle_duplicate(cli->entries_vmo, MX_RIGHT_SAME_RIGHTS, &srv->entries_vmo)) < 0) {
        goto fail;
    }

    mx_handle_close(fifo0);
    mx_handle_close(fifo1);
    return NO_ERROR;

fail:
    mx_handle_close(fifo0);
    mx_handle_close(fifo1);
    eth_ioring_destroy(cli);
    eth_ioring_destroy(srv);
    return status;
}