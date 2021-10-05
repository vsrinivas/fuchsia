// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SYNC_CALL_H_
#define LIB_FIDL_LLCPP_SYNC_CALL_H_

#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/llcpp/wire_messaging.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace fidl {

// Calculates the maximum possible message size for a FIDL type,
// clamped at the Zircon channel packet size.
// TODO(fxbug.dev/8093): Always request the message context.
template <typename FidlType, const MessageDirection Direction = MessageDirection::kReceiving>
constexpr uint32_t MaxSizeInChannel() {
  return internal::ClampedMessageSize<FidlType, Direction>();
}

// An buffer holding data inline, sized specifically for |FidlType|.
// It can be used to allocate request/response buffers when using the caller-allocate or in-place
// flavor. For example:
//
//     fidl::Buffer<mylib::FooRequest> request_buffer;
//     fidl::Buffer<mylib::FooResponse> response_buffer;
//     auto result = fidl::WireCall<mylib>(channel)->Foo(request_buffer.view(), args,
//     response_buffer.view());
//
// Since the |Buffer| type is always used at client side, we can assume responses are processed in
// the |kSending| context, and requests are processed in the |kReceiving| context.
template <typename FidlType>
using Buffer =
    internal::InlineMessageBuffer<internal::IsResponseType<FidlType>::value
                                      ? MaxSizeInChannel<FidlType, MessageDirection::kReceiving>()
                                      : MaxSizeInChannel<FidlType, MessageDirection::kSending>()>;

namespace internal {

// A veneer interface object for client/server messaging implementations that
// operate on a borrowed client/server endpoint. Those implementations should
// inherit from this class following CRTP. Example uses of this veneer:
//   * Making synchronous one-way or two-way calls.
//   * Sending events.
//
// |Derived| implementations must not add any state, only behavior.
//
// It must not outlive the borrowed endpoint.
template <typename Derived>
class SyncEndpointVeneer {
 public:
  explicit SyncEndpointVeneer(zx::unowned_channel channel) : channel_(std::move(channel)) {}

  // Returns a pointer to the concrete messaging implementation.
  Derived* operator->() && {
    // Required for the static_cast in to work: we are aliasing the base class
    // into |Derived|.
    static_assert(sizeof(Derived) == sizeof(SyncEndpointVeneer),
                  "Derived implementations must not add any state");

    return static_cast<Derived*>(this);
  }

  // TODO(fxbug.dev/85688): Add `.buffer(...)` for caller-allocating flavors.

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  zx::unowned_channel _channel() const { return zx::unowned_channel(channel_->get()); }

 private:
  zx::unowned_channel channel_;
};

}  // namespace internal

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::WireCall(client_end)->Method(args...);
//
// TODO(fxbug.dev/85688): Replace with
// |SyncEndpointVeneer<WireSyncClientImpl<FidlProtocol>>| after migrating
// non-fuchsia.git users.
template <typename FidlProtocol>
fidl::internal::WireSyncClientImpl<FidlProtocol> WireCall(
    const fidl::ClientEnd<FidlProtocol>& client_end) {
  return fidl::internal::WireSyncClientImpl<FidlProtocol>(client_end.borrow().channel());
}

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::WireCall(client_end)->Method(args...);
//
// TODO(fxbug.dev/85688): Replace with
// |SyncEndpointVeneer<WireSyncClientImpl<FidlProtocol>>| after migrating
// non-fuchsia.git users.
template <typename FidlProtocol>
fidl::internal::WireSyncClientImpl<FidlProtocol> WireCall(
    const fidl::UnownedClientEnd<FidlProtocol>& client_end) {
  return fidl::internal::WireSyncClientImpl<FidlProtocol>(client_end.channel());
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SYNC_CALL_H_
