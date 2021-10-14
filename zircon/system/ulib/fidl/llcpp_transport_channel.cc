// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport_channel.h>

namespace fidl {
namespace internal {

namespace {
void handle_close(Handle handle) { zx_handle_close(handle.value()); }
}  // namespace

const TransportVTable ChannelTransport::VTable = {
    .type = TransportType::Channel,
    .close = handle_close,
};

AnyTransport MakeAnyTransport(zx::channel channel) {
  return AnyTransport::Make<ChannelTransport>(Handle(channel.release()));
}
AnyUnownedTransport MakeAnyUnownedTransport(const zx::channel& channel) {
  return MakeAnyUnownedTransport(channel.borrow());
}
AnyUnownedTransport MakeAnyUnownedTransport(const zx::unowned_channel& channel) {
  return AnyUnownedTransport::Make<ChannelTransport>(Handle(channel->get()));
}

}  // namespace internal
}  // namespace fidl
