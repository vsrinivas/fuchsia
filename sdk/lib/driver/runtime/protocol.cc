// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/protocol.h>
#include <zircon/assert.h>

namespace fdf {

Protocol::Protocol(Handler handler) : fdf_token_t{CallHandler}, handler_(std::move(handler)) {}

Protocol::~Protocol() { ZX_DEBUG_ASSERT(!is_pending()); }

zx_status_t Protocol::Register(zx::channel token, fdf_dispatcher_t* dispatcher) {
  if (dispatcher_) {
    return ZX_ERR_BAD_STATE;
  }
  dispatcher_ = dispatcher;

  zx_status_t status = fdf_token_register(token.release(), dispatcher_, this);
  if (status != ZX_OK) {
    dispatcher_ = nullptr;
    return status;
  }
  return ZX_OK;
}

// static
void Protocol::CallHandler(fdf_dispatcher_t* dispatcher, fdf_token_t* token, zx_status_t status,
                           fdf_handle_t handle) {
  auto self = static_cast<Protocol*>(token);
  ZX_ASSERT(self->handler_);
  self->dispatcher_ = nullptr;
  self->handler_(dispatcher, self, status, fdf::Channel(handle));
}

zx_status_t ProtocolConnect(zx::channel token, fdf::Channel channel) {
  return fdf_token_transfer(token.release(), channel.release());
}

}  // namespace fdf
