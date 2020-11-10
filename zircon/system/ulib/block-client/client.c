// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/sync/completion.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

#include <block-client/client.h>

// Writes on a FIFO, repeating the write later if the FIFO is full.
static zx_status_t do_write(zx_handle_t fifo, block_fifo_request_t* request, size_t count) {
  zx_status_t status;
  while (true) {
    size_t actual;
    status = zx_fifo_write(fifo, sizeof(block_fifo_request_t), request, count, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      if ((status = zx_object_wait_one(fifo, ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
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

static zx_status_t do_read(zx_handle_t fifo, block_fifo_response_t* response, size_t* count) {
  zx_status_t status;
  while (true) {
    status = zx_fifo_read(fifo, sizeof(*response), response, *count, count);
    if (status == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      if ((status = zx_object_wait_one(fifo, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
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
  bool in_use;
  bool done;
  zx_status_t status;
} block_sync_completion_t;

typedef struct fifo_client {
  zx_handle_t fifo;
  block_sync_completion_t groups[MAX_TXN_GROUP_COUNT];
  mtx_t mutex;
  cnd_t condition;
  bool reading;
} fifo_client_t;

zx_status_t block_fifo_create_client(zx_handle_t fifo, fifo_client_t** out) {
  fifo_client_t* client = calloc(sizeof(fifo_client_t), 1);
  if (client == NULL) {
    zx_handle_close(fifo);
    return ZX_ERR_NO_MEMORY;
  }
  client->fifo = fifo;
  mtx_init(&client->mutex, mtx_plain);
  cnd_init(&client->condition);
  client->reading = false;
  for (int i = 0; i < MAX_TXN_GROUP_COUNT; ++i) {
    client->groups[i].in_use = false;
  }
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

  // Find a group we can use.
  groupid_t group;
  mtx_lock(&client->mutex);
  for (;;) {
    for (group = 0; group < MAX_TXN_GROUP_COUNT && client->groups[group].in_use; ++group) {
    }
    if (group < MAX_TXN_GROUP_COUNT) {
      break;
    }
    // No free groups so wait.
    cnd_wait(&client->condition, &client->mutex);
  }
  block_sync_completion_t* sync = &client->groups[group];
  sync->in_use = true;
  sync->done = false;
  sync->status = ZX_ERR_IO;
  mtx_unlock(&client->mutex);

  zx_status_t status;
  for (size_t i = 0; i < count; i++) {
    requests[i].group = group;
    requests[i].opcode = (requests[i].opcode & BLOCKIO_OP_MASK) | BLOCKIO_GROUP_ITEM;
  }

  requests[count - 1].opcode |= BLOCKIO_GROUP_LAST;

  if ((status = do_write(client->fifo, &requests[0], count)) != ZX_OK) {
    mtx_lock(&client->mutex);
    sync->in_use = false;
    mtx_unlock(&client->mutex);
    cnd_broadcast(&client->condition);
    return status;
  }

  // As expected by the protocol, when we send one "BLOCKIO_GROUP_LAST" message, we
  // must read a reply message.
  mtx_lock(&client->mutex);

  while (!sync->done) {
    // Only let one thread do the reading at a time.
    if (!client->reading) {
      client->reading = true;
      mtx_unlock(&client->mutex);

      block_fifo_response_t response[8];
      size_t count = 8;
      status = do_read(client->fifo, response, &count);

      mtx_lock(&client->mutex);
      client->reading = false;

      if (status != ZX_OK) {
        sync->in_use = false;
        mtx_unlock(&client->mutex);
        cnd_broadcast(&client->condition);
        return status;
      }

      // Record all the responses.
      for (size_t i = 0; i < count; ++i) {
        assert(client->groups[response[i].group].in_use);
        client->groups[response[i].group].status = response[i].status;
        client->groups[response[i].group].done = true;
      }
      cnd_broadcast(&client->condition);  // Signal all threads that might be waiting for responses.
    } else {
      cnd_wait(&client->condition, &client->mutex);
    }
  }

  // Free the group.
  status = sync->status;
  sync->in_use = false;
  mtx_unlock(&client->mutex);
  cnd_broadcast(&client->condition);  // Signal a thread that might be waiting for a free group.

  return status;
}
