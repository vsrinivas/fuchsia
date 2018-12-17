// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/overnet_stream.h>
#include <zircon/assert.h>

namespace fidl {

OvernetStream::~OvernetStream() = default;

zx_status_t OvernetStream::Process_(fidl::Message message) {
  if (!message.has_header()) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = Dispatch_(&message);
  if (status != ZX_ERR_NOT_SUPPORTED) {
    return status;
  }
  // API guarantees that message has not been mutated (as status ==
  // ZX_ERR_NOT_SUPPORTED)
  auto it = callbacks_.find(message.header().txid);
  if (it != callbacks_.end()) {
    auto fn = std::move(it->second);
    callbacks_.erase(it);
    fn(std::move(message));
    return ZX_OK;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
}

void OvernetStream::Send_(uint32_t txid, fidl::Message message) {
  ZX_DEBUG_ASSERT(message.txid() == 0);
  message.set_txid(txid);
  Send_(std::move(message));
}

zx_txid_t OvernetStream::AllocateCallback(
    fit::function<zx_status_t(fidl::Message)> callback) {
  zx_txid_t id;
  do {
    // No need to worry about user space txid spaces - OvernetStream messages
    // are never carried over channels.
    id = next_txid_++;
  } while (callbacks_.count(id));
  callbacks_.emplace(id, std::move(callback));
  return id;
}

}  // namespace fidl
