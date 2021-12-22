// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_CLIENT_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_CLIENT_H_

#include <lib/zx/fifo.h>
#include <lib/zx/status.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <fbl/macros.h>

#include "src/lib/storage/block_client/cpp/client_c.h"

namespace block_client {

class Client {
 public:
  // Constructs an invalid Client.
  //
  // It is invalid to call any block client operations with this empty block client wrapper.
  Client();

  Client(Client&& other);
  Client& operator=(Client&& other);
  ~Client();

  // Factory function.
  static zx::status<Client> Create(zx::fifo fifo);

  // Issues a group of block requests over the underlying fifo, and waits for a response.
  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) const;

 private:
  explicit Client(fifo_client_t* client);

  // Replace the current fifo_client with a new one.
  void Reset(fifo_client_t* client = nullptr);

  // Relinquish the underlying fifo client without destroying it.
  fifo_client_t* Release();

  fifo_client_t* client_;
};

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_CLIENT_H_
