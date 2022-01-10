// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_CLIENT_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_CLIENT_H_

#include <lib/zx/fifo.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <condition_variable>
#include <mutex>

namespace block_client {

// Provides a simple synchronous wrapper for talking to a block device over a FIFO.
//
// Block devices can support several (MAX_TXN_GROUP_COUNT) requests in-flight at once and this class
// is threadsafe to support this many requests from different threads in parallel. Exceeding
// MAX_TXN_GROUP_COUNT parallel transactions will block future requests until a transaction group
// becomes available.
class Client {
 public:
  explicit Client(zx::fifo fifo);

  // This object can not be moved due to the presence of the mutex.
  Client(const Client&) = delete;
  Client(Client&&) = delete;

  // Issues a group of block requests over the underlying fifo, and waits for a response.
  zx_status_t Transaction(block_fifo_request_t* requests, size_t count);

 private:
  struct BlockCompletion {
    bool in_use = false;
    bool done = false;
    zx_status_t status = ZX_ERR_IO;
  };

  zx_status_t DoRead(block_fifo_response_t* response, size_t* count);
  zx_status_t DoWrite(block_fifo_request_t* request, size_t count);

  zx::fifo fifo_;

  std::mutex mutex_;

  BlockCompletion groups_[MAX_TXN_GROUP_COUNT];  // Guarded by mutex.

  std::condition_variable condition_;
  bool reading_ = false;  // Guarded by mutex.
};

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_CLIENT_H_
