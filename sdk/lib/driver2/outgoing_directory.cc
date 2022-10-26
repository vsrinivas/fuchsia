// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/handlers.h>
#include <lib/driver2/outgoing_directory.h>

namespace driver {

OutgoingDirectory::OutgoingDirectory(OutgoingDirectory&& other) noexcept
    : component_outgoing_dir_(std::move(other.component_outgoing_dir_)),
      dispatcher_(other.dispatcher_) {
  other.dispatcher_ = nullptr;
}

OutgoingDirectory& OutgoingDirectory::operator=(OutgoingDirectory&& other) noexcept {
  component_outgoing_dir_ = std::move(other.component_outgoing_dir_);
  dispatcher_ = other.dispatcher_;

  other.dispatcher_ = nullptr;

  return *this;
}

void OutgoingDirectory::RegisterRuntimeToken(zx::channel token, AnyHandler handler) {
  auto token_connect_handler = [handler = std::move(handler)](
                                   fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol,
                                   zx_status_t status, fdf::Channel channel) mutable {
    if (status == ZX_OK) {
      ZX_ASSERT(channel.is_valid());
      handler(std::move(channel));
    }
    delete protocol;  // Recover the released unique_ptr.
  };

  auto protocol = std::make_unique<fdf::Protocol>(std::move(token_connect_handler));
  // We do not assert ZX_OK, as this may fail in the case where the dispatcher is shutting down.
  auto status = protocol->Register(std::move(token), dispatcher_);
  if (status == ZX_OK) {
    protocol.release();  // Will be deleted in the callback.
  }
}

}  // namespace driver
