// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <block-client/cpp/client.h>
#include <block-client/client.h>
#include <fbl/macros.h>
#include <fbl/type_support.h>
#include <lib/zx/fifo.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

namespace block_client {

Client::Client() : client_(nullptr) {}
Client::Client(fifo_client_t* client) : client_(client) {}
Client::Client(Client&& other) : client_(other.Release()) {}

Client& Client::operator=(Client&& other) {
    Reset(other.Release());
    return *this;
}

Client::~Client() {
    Reset();
}

zx_status_t Client::Create(zx::fifo fifo, Client* out) {
    fifo_client_t* client;
    zx_status_t status = block_fifo_create_client(fifo.release(), &client);
    if (status != ZX_OK) {
        return status;
    }
    *out = Client(client);
    return ZX_OK;
}

zx_status_t Client::Transaction(block_fifo_request_t* requests, size_t count) const {
    ZX_DEBUG_ASSERT(client_ != nullptr);
    return block_fifo_txn(client_, requests, count);
}

void Client::Reset(fifo_client_t* client) {
    if (client_ != nullptr) {
        block_fifo_release_client(client_);
    }
    client_ = client;
}

fifo_client_t* Client::Release() {
    fifo_client_t* client = client_;
    client_ = nullptr;
    return client;
}

}  // namespace block_client
