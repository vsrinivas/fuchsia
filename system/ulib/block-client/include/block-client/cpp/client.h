// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __cplusplus
#error "C++ Only file"
#endif  // __cplusplus

#include <stdlib.h>

#include <block-client/client.h>
#include <fbl/macros.h>
#include <fbl/type_support.h>
#include <lib/zx/fifo.h>
#include <zircon/types.h>

namespace block_client {

class Client {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Client);

    // Constructs an invalid Client.
    //
    // It is invalid to call any block client operations with this
    // empty block client wrapper.
    Client();

    // Constructs a valid Client, capable of issuing
    // block client operations.
    explicit Client(fifo_client_t* client);
    Client(Client&& other);
    Client& operator=(Client&& other);
    ~Client();

    // Initializer for a BlockClient, which, on success,
    // will make |out| a valid Client.
    static zx_status_t Create(zx::fifo fifo, Client* out);

    // BLOCK CLIENT OPERATIONS.

    // Issues a group of block requests over the underlying fifo,
    // and waits for a response.
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) const;

private:
    // Replace the current fifo_client with a new one.
    void Reset(fifo_client_t* client = nullptr);

    // Relinquish the underlying fifo client without destroying it.
    fifo_client_t* Release();

    fifo_client_t* client_;
};

}  // namespace block_client
