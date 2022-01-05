// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport.h>

namespace fidl {
namespace internal {

void* TransportContextBase::release(const TransportVTable* vtable) {
  ZX_ASSERT(vtable && vtable_ && vtable_->type == vtable->type);

  void* data = data_;
  vtable_ = nullptr;
  data_ = nullptr;
  return data;
}

IncomingTransportContext::~IncomingTransportContext() {
  if (vtable_ && vtable_->close_incoming_transport_context) {
    vtable_->close_incoming_transport_context(data_);
  }
}

OutgoingTransportContext::~OutgoingTransportContext() {
  if (vtable_ && vtable_->close_outgoing_transport_context) {
    vtable_->close_outgoing_transport_context(data_);
  }
}

AnyUnownedTransport MakeAnyUnownedTransport(const AnyTransport& transport) {
  return transport.borrow();
}

}  // namespace internal
}  // namespace fidl
