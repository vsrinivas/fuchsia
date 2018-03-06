// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/synchronous_proxy.h"

#include <stdio.h>

#include <memory>
#include <utility>

namespace fidl {
namespace internal {

SynchronousProxy::SynchronousProxy(zx::channel channel)
    : channel_(std::move(channel)), next_txid_(1) {}

SynchronousProxy::~SynchronousProxy() = default;

zx::channel SynchronousProxy::TakeChannel() {
  return std::move(channel_);
}

zx_status_t SynchronousProxy::Send(const fidl_type_t* type, Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "error: fidl_valdate: %s\n", error_msg);
    return status;
  }
  return message.Write(channel_.get(), 0);
}

zx_status_t SynchronousProxy::Call(const fidl_type_t* request_type,
                                   const fidl_type_t* response_type,
                                   Message request,
                                   Message* response) {
  request.set_txid(GetNextTxid());
  const char* error_msg = nullptr;
  zx_status_t status = request.Validate(request_type, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "error: fidl_valdate: %s\n", error_msg);
    return status;
  }
  status = request.Call(channel_.get(), 0, ZX_TIME_INFINITE, nullptr, response);
  if (status != ZX_OK)
    return status;
  status = response->Decode(response_type, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "error: fidl_decode: %s\n", error_msg);
    return status;
  }
  return ZX_OK;
}

zx_txid_t SynchronousProxy::GetNextTxid() {
  zx_txid_t txid = 0;
  while (!txid) {
    txid = next_txid_.fetch_add(1, std::memory_order_relaxed);
  }
  return txid;
}

}  // namespace internal
}  // namespace fidl
