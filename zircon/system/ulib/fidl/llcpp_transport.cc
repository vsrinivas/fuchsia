// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>

namespace fidl {
namespace internal {

AnyUnownedTransport MakeAnyUnownedTransport(const AnyTransport& transport) {
  return transport.borrow();
}

// TODO(fxbug.dev/85734) Remove dependency on transport_channel.h from this file.
const TransportVTable* LookupTransportVTable(fidl_transport_type type) {
  switch (type) {
    case FIDL_TRANSPORT_TYPE_INVALID:
      ZX_PANIC("No VTable for FIDL_TRANSPORT_TYPE_INVALID");
      break;
    case FIDL_TRANSPORT_TYPE_CHANNEL:
      return &ChannelTransport::VTable;
    default:
      ZX_PANIC("Unknown transport type");
      break;
  }
}

}  // namespace internal
}  // namespace fidl
