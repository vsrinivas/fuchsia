// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_CLIENT_DETAILS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_CLIENT_DETAILS_H_

#include <lib/fdf/cpp/arena.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl_driver/cpp/wire_messaging_declarations.h>

namespace fdf {
namespace internal {

// |BufferClientImplBase| stores the core state for client messaging
// implementations that use |ClientBase|, and where the message encoding buffers
// are provided by an allocator.
class BufferClientImplBase {
 public:
  explicit BufferClientImplBase(fidl::internal::ClientBase* client_base, const fdf::Arena& arena)
      : client_base_(client_base), arena_(arena) {}

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  fidl::internal::ClientBase* _client_base() const { return client_base_; }

  // Used by implementations to access the arena, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  const fdf::Arena& _arena() { return arena_; }

 private:
  fidl::internal::ClientBase* client_base_;
  const fdf::Arena& arena_;
};

}  // namespace internal
}  // namespace fdf

namespace fidl::internal {
template <typename Protocol>
AnyIncomingEventDispatcher MakeAnyEventDispatcher(
    fdf::WireAsyncEventHandler<Protocol>* event_handler) {
  AnyIncomingEventDispatcher event_dispatcher;
  event_dispatcher.emplace<fidl::internal::WireEventDispatcher<Protocol>>(event_handler);
  return event_dispatcher;
}
}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_CLIENT_DETAILS_H_
