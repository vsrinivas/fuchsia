// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/client.h"

#include <lib/zx/fifo.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <fbl/macros.h>

namespace block_client {

Client::Client(zx::fifo fifo) : fifo_(std::move(fifo)) {}

zx_status_t Client::Transaction(block_fifo_request_t* requests, size_t count) {
  if (count == 0)
    return ZX_OK;

  // Find a group we can use.
  BlockCompletion* block_completion = nullptr;
  groupid_t group;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      for (group = 0; group < MAX_TXN_GROUP_COUNT && groups_[group].in_use; ++group) {
      }
      if (group < MAX_TXN_GROUP_COUNT)
        break;  // Found a free one.

      // No free groups so wait.
      condition_.wait(lock);
    }
    block_completion = &groups_[group];
    block_completion->in_use = true;
    block_completion->done = false;
    block_completion->status = ZX_ERR_IO;
  }

  for (size_t i = 0; i < count; i++) {
    requests[i].group = group;
    requests[i].opcode = (requests[i].opcode & BLOCKIO_OP_MASK) | BLOCKIO_GROUP_ITEM;
  }

  requests[count - 1].opcode |= BLOCKIO_GROUP_LAST;

  if (zx_status_t status = DoWrite(requests, count); status != ZX_OK) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      block_completion->in_use = false;
    }
    condition_.notify_all();
    return status;
  }

  // As expected by the protocol, when we send one "BLOCKIO_GROUP_LAST" message, we
  // must read a reply message.
  zx_status_t status = ZX_OK;
  {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!block_completion->done) {
      // Only let one thread do the reading at a time.
      if (!reading_) {
        reading_ = true;

        constexpr size_t kMaxResponseCount = 8;
        block_fifo_response_t response[kMaxResponseCount];
        size_t count = kMaxResponseCount;

        // Unlocked block.
        {
          lock.unlock();
          status = DoRead(response, &count);
          lock = std::unique_lock<std::mutex>(mutex_);
        }
        reading_ = false;

        if (status != ZX_OK) {
          block_completion->in_use = false;
          lock.unlock();
          condition_.notify_all();
          return status;
        }

        // Record all the responses.
        for (size_t i = 0; i < count; ++i) {
          assert(groups_[response[i].group].in_use);
          groups_[response[i].group].status = response[i].status;
          groups_[response[i].group].done = true;
        }
        condition_.notify_all();  // Signal all threads that might be waiting for responses.
      } else {
        condition_.wait(lock);
      }
    }

    // Free the group.
    status = block_completion->status;
    block_completion->in_use = false;
  }
  condition_.notify_all();  // Signal a thread that might be waiting for a free group.

  return status;
}

zx_status_t Client::DoRead(block_fifo_response_t* response, size_t* count) {
  while (true) {
    if (zx_status_t status = fifo_.read(sizeof(*response), response, *count, count);
        status != ZX_ERR_SHOULD_WAIT)
      return status;

    zx_signals_t signals;
    if (zx_status_t status =
            fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite(), &signals);
        status != ZX_OK) {
      return status;
    }
    if (signals & ZX_FIFO_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    }
    // Try reading again...
  }
}

zx_status_t Client::DoWrite(block_fifo_request_t* request, size_t count) {
  while (true) {
    size_t actual;
    zx_status_t write_status = fifo_.write(sizeof(block_fifo_request_t), request, count, &actual);
    if (write_status == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      if (zx_status_t wait_status = fifo_.wait_one(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                                                   zx::time::infinite(), &signals);
          wait_status != ZX_OK) {
        return wait_status;
      }
      if (signals & ZX_FIFO_PEER_CLOSED) {
        return ZX_ERR_PEER_CLOSED;
      }
      // Try writing again...
    } else if (write_status == ZX_OK) {
      count -= actual;
      request += actual;
      if (count == 0)
        return ZX_OK;
    } else {
      return write_status;
    }
  }
}

}  // namespace block_client
