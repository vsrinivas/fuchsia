// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// channel.h is the "entrypoint header" that should be included when using the
// channel transport with the unified bindings.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_CHANNEL_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_CHANNEL_H_

#include <lib/fidl/cpp/client.h>
#include <lib/fidl/cpp/internal/channel_endpoint_conversions.h>
#include <lib/fidl/cpp/unified_messaging.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/internal/arrow.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/server.h>

namespace fidl {

//
// Note: when updating the documentation below, please make similar updates to
// the one in //sdk/lib/fidl/cpp/wire/include/lib/fidl/cpp/wire/channel.h
//
// The interface documentation on |fidl::SyncClient| is largely identical to
// those on |fidl::WireSyncClient|, after removing the "wire" portion from
// comments.
//

// |fidl::SyncClient| owns a client endpoint and exposes synchronous FIDL calls
// taking both natural and wire types. Prefer using this owning class over
// |fidl::Call| unless one has to interface with very low-level functionality
// (such as making a call over a raw zx_handle_t).
//
// Generated FIDL APIs are accessed by 'dereferencing' the client value:
//
//     // Creates a sync client that speaks over |client_end|.
//     fidl::SyncClient client(std::move(client_end));
//
//     // Call the |Foo| method synchronously, obtaining the results from the
//     // return value.
//     fidl::Result result = client->Foo(args);
//
// |fidl::SyncClient| is suitable for code without access to an async
// dispatcher.
//
// |fidl::SyncClient| includes a superset of the functionality of
// |fidl::WireSyncClient|, which only exposes synchronous FIDL calls with wire
// types. Prefer |fidl::SyncClient| over |fidl::WireSyncClient| unless your
// application needs to statically enforce that only the more performant wire
// types are used.
//
// ## Thread safety
//
// |SyncClient| is generally thread-safe with a few caveats:
//
// - Client objects can be safely sent between threads.
// - One may invoke many FIDL methods in parallel on the same client. However,
//   FIDL method calls must be synchronized with operations that consume or
//   mutate the client object itself:
//
//     - Calling `Bind` or `TakeClientEnd`.
//     - Assigning a new value to the |SyncClient| variable.
//     - Moving the |SyncClient| to a different location.
//     - Destroying the |SyncClient|.
//
// - There can be at most one `HandleOneEvent` call going on at the same time.
template <typename FidlProtocol>
class SyncClient : private WireSyncClient<FidlProtocol> {
 private:
  using Base = WireSyncClient<FidlProtocol>;

 public:
  // Creates an uninitialized client that is not bound to a client endpoint.
  //
  // Prefer using the constructor overload that initializes the client
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before an endpoint could be obtained (for
  // example, if the client is an instance variable).
  //
  // The client may be initialized later via |Bind|.
  SyncClient() = default;

  // Creates an initialized client. FIDL calls will be made on |client_end|.
  //
  // Similar to |fidl::Client|, the client endpoint must be valid.
  //
  // To just make a FIDL call uniformly on a client endpoint that may or may not
  // be valid, use the |fidl::Call(client_end)| helper. We may extend
  // |fidl::SyncClient<P>| with richer features hinging on having a valid
  // endpoint in the future.
  explicit SyncClient(::fidl::ClientEnd<FidlProtocol> client_end) : Base(std::move(client_end)) {}

  ~SyncClient() = default;
  SyncClient(SyncClient&&) noexcept = default;
  SyncClient& operator=(SyncClient&&) noexcept = default;

  // Whether the client is initialized.
  bool is_valid() const { return Base::is_valid(); }
  explicit operator bool() const { return Base::operator bool(); }

  // Borrows the underlying client endpoint. The client must have been
  // initialized.
  const ::fidl::ClientEnd<FidlProtocol>& client_end() const { return Base::client_end(); }

  // Initializes the client with a |client_end|. FIDL calls will be made on this
  // endpoint.
  //
  // It is not allowed to call |Bind| on an initialized client. To rebind a
  // |SyncClient| to a different endpoint, simply replace the
  // |SyncClient| variable with a new instance.
  void Bind(::fidl::ClientEnd<FidlProtocol> client_end) { Base::Bind(std::move(client_end)); }

  // Extracts the underlying endpoint from the client. After this operation, the
  // client goes back to an uninitialized state.
  //
  // It is not safe to invoke this method while there are ongoing FIDL calls.
  ::fidl::ClientEnd<FidlProtocol> TakeClientEnd() { return Base::TakeClientEnd(); }

  // Returns the interface for making FIDL calls with natural objects.
  internal::SyncEndpointManagedVeneer<internal::NaturalSyncClientImpl<FidlProtocol>> operator->()
      const {
    ZX_ASSERT(is_valid());
    return internal::SyncEndpointManagedVeneer<internal::NaturalSyncClientImpl<FidlProtocol>>(
        fidl::internal::MakeAnyUnownedTransport(client_end().handle()));
  }

  // Returns the interface for making outgoing FIDL calls using wire objects.
  // The client must be initialized first.
  const Base& wire() const { return *this; }

  // Handle all possible events defined in this protocol.
  //
  // Blocks to consume exactly one message from the channel, then call the corresponding virtual
  // method defined in |event_handler|. If the message was unknown or malformed, returns an
  // error without calling any virtual method.
  ::fidl::Status HandleOneEvent(fidl::SyncEventHandler<FidlProtocol>& event_handler) const {
    return event_handler.HandleOneEvent(client_end());
  }
};

template <typename FidlProtocol>
SyncClient(fidl::ClientEnd<FidlProtocol>) -> SyncClient<FidlProtocol>;

// |Call| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::Call(client_end)->Method(request);
//
template <typename FidlProtocol>
internal::SyncEndpointManagedVeneer<internal::NaturalSyncClientImpl<FidlProtocol>> Call(
    const fidl::ClientEnd<FidlProtocol>& client_end) {
  return internal::SyncEndpointManagedVeneer<internal::NaturalSyncClientImpl<FidlProtocol>>(
      fidl::internal::MakeAnyUnownedTransport(client_end.borrow().handle()));
}

// |Call| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::Call(client_end)->Method(request);
//
template <typename FidlProtocol>
internal::SyncEndpointManagedVeneer<internal::NaturalSyncClientImpl<FidlProtocol>> Call(
    const fidl::UnownedClientEnd<FidlProtocol>& client_end) {
  return internal::SyncEndpointManagedVeneer<internal::NaturalSyncClientImpl<FidlProtocol>>(
      fidl::internal::MakeAnyUnownedTransport(client_end.handle()));
}

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
// over the endpoint managed by |binding|. Call it like:
//
//     fidl::ServerBinding<SomeProtocol> server_binding_{...};
//     fidl::SendEvent(server_binding_)->FooEvent(args...);
//
template <typename FidlProtocol>
auto SendEvent(const ServerBinding<FidlProtocol>& binding) {
  return internal::Arrow<internal::NaturalWeakEventSender<FidlProtocol>>(internal::BorrowBinding(
      static_cast<const fidl::internal::ServerBindingBase<FidlProtocol>&>(binding)));
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
