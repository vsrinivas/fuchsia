// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// channel.h is the "entrypoint header" that should be included when using the
// channel transport with the unified bindings.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_CHANNEL_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_CHANNEL_H_

#include <lib/fidl/cpp/client.h>
#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/fidl/llcpp/internal/arrow.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/server.h>

namespace fidl {

// Return an interface for sending FIDL events containing natural domain objects
// over the endpoint managed by |binding_ref|. Call it like:
//
//     fidl::SendEvent(server_binding_ref)->FooEvent(event_body);
//
template <typename FidlProtocol>
auto SendEvent(const ServerBindingRef<FidlProtocol>& binding_ref) {
  return internal::Arrow<internal::NaturalWeakEventSender<FidlProtocol>>(internal::BorrowBinding(
      static_cast<const fidl::internal::ServerBindingRefBase&>(binding_ref)));
}

// Return an interface for sending FIDL events containing natural domain objects
// over |server_end|. Call it like:
//
//     fidl::SendEvent(server_end)->FooEvent(event_body);
//
template <typename FidlProtocol>
auto SendEvent(const ServerEnd<FidlProtocol>& server_end) {
  return internal::Arrow<internal::NaturalEventSender<FidlProtocol>>(
      fidl::internal::MakeAnyUnownedTransport(server_end.channel()));
}

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_CHANNEL_H_
