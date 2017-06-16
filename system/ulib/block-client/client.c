// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/device/block.h>
#include <magenta/syscalls.h>
#include <sync/completion.h>

#include "block-client/client.h"

// Writes on a FIFO, repeating the write later if the FIFO is full.
static mx_status_t do_write(mx_handle_t fifo, block_fifo_request_t* request, size_t count) {
    mx_status_t status;
    while (true) {
        uint32_t actual;
        status = mx_fifo_write(fifo, request, sizeof(block_fifo_request_t) * count, &actual);
        if (status == MX_ERR_SHOULD_WAIT) {
            mx_signals_t signals;
            if ((status = mx_object_wait_one(fifo,
                                             MX_FIFO_WRITABLE | MX_FIFO_PEER_CLOSED,
                                             MX_TIME_INFINITE, &signals)) != MX_OK) {
                return status;
            } else if (signals & MX_FIFO_PEER_CLOSED) {
                return MX_ERR_PEER_CLOSED;
            }
            // Try writing again...
        } else if (status == MX_OK) {
            count -= actual;
            request += actual;
            if (count == 0) {
                return MX_OK;
            }
        } else {
            return status;
        }
    }
}

static mx_status_t do_read(mx_handle_t fifo, block_fifo_response_t* response) {
    mx_status_t status;
    while (true) {
        uint32_t count;
        status = mx_fifo_read(fifo, response, sizeof(block_fifo_response_t), &count);
        if (status == MX_ERR_SHOULD_WAIT) {
            mx_signals_t signals;
            if ((status = mx_object_wait_one(fifo,
                                             MX_FIFO_READABLE | MX_FIFO_PEER_CLOSED,
                                             MX_TIME_INFINITE, &signals)) != MX_OK) {
                return status;
            } else if (signals & MX_FIFO_PEER_CLOSED) {
                return MX_ERR_PEER_CLOSED;
            }
            // Try reading again...
        } else {
            return status;
        }
    }
}

typedef struct block_completion {
    completion_t completion;
    mx_status_t status;
} block_completion_t;

typedef struct fifo_client {
    mx_handle_t fifo;
    block_completion_t txns[MAX_TXN_COUNT];
} fifo_client_t;

mx_status_t block_fifo_create_client(mx_handle_t fifo, fifo_client_t** out) {
    fifo_client_t* client = calloc(sizeof(fifo_client_t), 1);
    if (client == NULL) {
        return MX_ERR_NO_MEMORY;
    }
    client->fifo = fifo;
    *out = client;
    return MX_OK;
}

void block_fifo_release_client(fifo_client_t* client) {
    if (client == NULL) {
        return;
    }

    mx_handle_close(client->fifo);
    free(client);
}

mx_status_t block_fifo_txn(fifo_client_t* client, block_fifo_request_t* requests, size_t count) {
    if (count == 0) {
        return MX_OK;
    } else if (count > MAX_TXN_MESSAGES) {
        return MX_ERR_INVALID_ARGS;
    }

    txnid_t txnid = requests[0].txnid;
    assert(txnid < MAX_TXN_COUNT);
    completion_reset(&client->txns[txnid].completion);
    client->txns[txnid].status = MX_ERR_IO;

    mx_status_t status;
    for (size_t i = 0; i < count; i++) {
        assert(requests[i].txnid == txnid);
        requests[i].opcode = (requests[i].opcode & BLOCKIO_OP_MASK) |
                             (i == count - 1 ? BLOCKIO_TXN_END : 0);
    }
    if ((status = do_write(client->fifo, &requests[0], count)) != MX_OK) {
        return status;
    }

    // As expected by the protocol, when we send one "BLOCKIO_TXN_END" message, we
    // must read a reply message.
    block_fifo_response_t response;
    if ((status = do_read(client->fifo, &response)) != MX_OK) {
        return status;
    }

    // Wake up someone who is waiting (it might be ourselves)
    txnid_t response_txnid = response.txnid;
    client->txns[response_txnid].status = response.status;
    completion_signal(&client->txns[response_txnid].completion);

    // Wait for someone to signal us
    completion_wait(&client->txns[txnid].completion, MX_TIME_INFINITE);

    return client->txns[txnid].status;
}
