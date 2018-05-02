// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <sync/completion.h>

#include "block-client/client.h"

// Writes on a FIFO, repeating the write later if the FIFO is full.
static zx_status_t do_write(zx_handle_t fifo, block_fifo_request_t* request, size_t count) {
    zx_status_t status;
    while (true) {
        size_t actual;
        status = zx_fifo_write(fifo, sizeof(block_fifo_request_t), request, count, &actual);
        if (status == ZX_ERR_SHOULD_WAIT) {
            zx_signals_t signals;
            if ((status = zx_object_wait_one(fifo,
                                             ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                                             ZX_TIME_INFINITE, &signals)) != ZX_OK) {
                return status;
            } else if (signals & ZX_FIFO_PEER_CLOSED) {
                return ZX_ERR_PEER_CLOSED;
            }
            // Try writing again...
        } else if (status == ZX_OK) {
            count -= actual;
            request += actual;
            if (count == 0) {
                return ZX_OK;
            }
        } else {
            return status;
        }
    }
}

static zx_status_t do_read(zx_handle_t fifo, block_fifo_response_t* response) {
    zx_status_t status;
    while (true) {
        status = zx_fifo_read(fifo, sizeof(*response), response, 1, NULL);
        if (status == ZX_ERR_SHOULD_WAIT) {
            zx_signals_t signals;
            if ((status = zx_object_wait_one(fifo,
                                             ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                             ZX_TIME_INFINITE, &signals)) != ZX_OK) {
                return status;
            } else if (signals & ZX_FIFO_PEER_CLOSED) {
                return ZX_ERR_PEER_CLOSED;
            }
            // Try reading again...
        } else {
            return status;
        }
    }
}

typedef struct block_completion {
    completion_t completion;
    zx_status_t status;
} block_completion_t;

typedef struct fifo_client {
    zx_handle_t fifo;
    block_completion_t groups[MAX_TXN_GROUP_COUNT];
} fifo_client_t;

zx_status_t block_fifo_create_client(zx_handle_t fifo, fifo_client_t** out) {
    fifo_client_t* client = calloc(sizeof(fifo_client_t), 1);
    if (client == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    client->fifo = fifo;
    *out = client;
    return ZX_OK;
}

void block_fifo_release_client(fifo_client_t* client) {
    if (client == NULL) {
        return;
    }

    zx_handle_close(client->fifo);
    free(client);
}

zx_status_t block_fifo_txn(fifo_client_t* client, block_fifo_request_t* requests, size_t count) {
    if (count == 0) {
        return ZX_OK;
    }

    groupid_t group = requests[0].group;
    assert(group < MAX_TXN_GROUP_COUNT);
    completion_reset(&client->groups[group].completion);
    client->groups[group].status = ZX_ERR_IO;

    zx_status_t status;
    for (size_t i = 0; i < count; i++) {
        assert(requests[i].group == group);
        requests[i].opcode = (requests[i].opcode & BLOCKIO_OP_MASK) | BLOCKIO_GROUP_ITEM;
    }

    requests[0].opcode |= BLOCKIO_BARRIER_BEFORE;
    requests[count - 1].opcode |= BLOCKIO_GROUP_LAST | BLOCKIO_BARRIER_AFTER;

    if ((status = do_write(client->fifo, &requests[0], count)) != ZX_OK) {
        return status;
    }

    // As expected by the protocol, when we send one "BLOCKIO_GROUP_LAST" message, we
    // must read a reply message.
    block_fifo_response_t response;
    if ((status = do_read(client->fifo, &response)) != ZX_OK) {
        return status;
    }

    // Wake up someone who is waiting (it might be ourselves)
    groupid_t response_group = response.group;
    client->groups[response_group].status = response.status;
    completion_signal(&client->groups[response_group].completion);

    // Wait for someone to signal us.
    completion_wait(&client->groups[group].completion, ZX_TIME_INFINITE);

    return client->groups[group].status;
}
