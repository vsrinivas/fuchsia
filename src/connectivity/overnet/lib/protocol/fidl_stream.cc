// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/fidl_stream.h"
#include <zircon/assert.h>

namespace overnet {

FidlStream::~FidlStream() = default;

zx_status_t FidlStream::Process_(fidl::Message message) {
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

void FidlStream::Send_(uint32_t txid, fidl::Message message) {
  ZX_DEBUG_ASSERT(message.txid() == 0);
  message.set_txid(txid);
  Send_(std::move(message));
}

zx_txid_t FidlStream::AllocateCallback(
    fit::function<zx_status_t(fidl::Message)> callback) {
  zx_txid_t id;
  do {
    // No need to worry about user space txid spaces - FidlStream messages
    // are never carried over channels.
    id = next_txid_++;
  } while (id == 0 || callbacks_.count(id));
  callbacks_.emplace(id, std::move(callback));
  return id;
}

}  // namespace overnet
