// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SYNC_CALL_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SYNC_CALL_H_

#include <lib/fidl/cpp/wire/sync_call.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fdf {
namespace internal {
// A veneer interface object for client/server messaging implementations that
// operate on a borrowed client/server endpoint. This is used for sync calls.
//
// |Derived| implementations must not add any state, only behavior.
template <typename Derived>
struct SyncEndpointBufferVeneer {
  explicit SyncEndpointBufferVeneer(fidl::internal::AnyUnownedTransport transport,
                                    const fdf::Arena& arena)
      : transport_(transport), arena_(arena) {}

  // Returns a pointer to the concrete messaging implementation.
  Derived* operator->() {
    // Required for the static_cast in to work: we are aliasing the base class
    // into |Derived|.
    static_assert(sizeof(Derived) == sizeof(SyncEndpointBufferVeneer),
                  "Derived implementations must not add any state");

    return static_cast<Derived*>(this);
  }

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  fidl::internal::AnyUnownedTransport _transport() const { return transport_; }

  // Used by implementations to access the arena, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  const fdf::Arena& _arena() { return arena_; }

 private:
  fidl::internal::AnyUnownedTransport transport_;
  const fdf::Arena& arena_;
};

// A veneer interface object for client/server messaging implementations that
// operate on a borrowed client/server endpoint.
//
// |SyncImpl| should be the template messaging class,
// e.g. |WireSyncClientImpl| (without passing template parameters).
// |FidlProtocol| should be the protocol marker.
//
// It must not outlive the borrowed endpoint.
template <typename FidlProtocol>
class SyncEndpointVeneer final {
 public:
  explicit SyncEndpointVeneer(fidl::internal::AnyUnownedTransport transport)
      : transport_(std::move(transport)) {}

  // Returns a veneer object which exposes the caller-allocating API, using
  // the provided |arena| to allocate buffers necessary for each call.
  // The requests and responses (if applicable) will live on the arena.
  auto buffer(const fdf::Arena& arena) {
    return SyncEndpointBufferVeneer<fidl::internal::WireSyncBufferClientImpl<FidlProtocol>>(
        transport_, arena);
  }

 private:
  fidl::internal::AnyUnownedTransport transport_;
};
}  // namespace internal

// |fdf::WireSyncClient| owns a client endpoint and exposes synchronous FIDL
// calls. Prefer using this owning class over |fdf::WireCall| unless one has to
// interface with very low-level functionality (such as making a call over a raw
// zx_handle_t).
//
// Generated FIDL APIs are accessed by 'dereferencing' the client value:
//
//     // Creates a sync client that speaks over |client_end|.
//     fdf::WireSyncClient client(std::move(client_end));
//
//     // Call the |Foo| method synchronously, obtaining the results from the
//     // return value.
//     fdf::Arena arena('EXAM');
//     fidl::WireResult result = client.buffer(arena)->Foo(args);
//
// |fdf::WireSyncClient| is suitable for code without access to an fdf
// dispatcher.
//
// ## Thread safety
//
// |WireSyncClient| is generally thread-safe with a few caveats:
//
// - Client objects can be safely sent between threads.
// - One may invoke many FIDL methods in parallel on the same client. However,
//   FIDL method calls must be synchronized with operations that consume or
//   mutate the client object itself:
//
//     - Calling `Bind` or `TakeClientEnd`.
//     - Assigning a new value to the |WireSyncClient| variable.
//     - Moving the |WireSyncClient| to a different location.
//     - Destroying the |WireSyncClient|.
//
template <typename FidlProtocol>
class WireSyncClient {
 public:
  // Creates an uninitialized client that is not bound to a client endpoint.
  //
  // Prefer using the constructor overload that initializes the client
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before an endpoint could be obtained (for
  // example, if the client is an instance variable).
  //
  // The client may be initialized later via |Bind|.
  WireSyncClient() = default;

  // Creates an initialized client. FIDL calls will be made on |client_end|.
  //
  // Similar to |fdf::WireClient|, the client endpoint must be valid.
  //
  // To just make a FIDL call uniformly on a client endpoint that may or may not
  // be valid, use the |fdf::WireCall(client_end)| helper. We may extend
  // |fdf::WireSyncClient<P>| with richer features hinging on having a valid
  // endpoint in the future.
  explicit WireSyncClient(fdf::ClientEnd<FidlProtocol> client_end)
      : client_end_(std::move(client_end)) {
    ZX_ASSERT(is_valid());
  }

  ~WireSyncClient() = default;
  WireSyncClient(WireSyncClient&&) noexcept = default;
  WireSyncClient& operator=(WireSyncClient&&) noexcept = default;

  // Whether the client is initialized.
  bool is_valid() const { return client_end_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // Borrows the underlying client endpoint. The client must have been
  // initialized.
  const fdf::ClientEnd<FidlProtocol>& client_end() const {
    ZX_ASSERT(is_valid());
    return client_end_;
  }

  // Initializes the client with a |client_end|. FIDL calls will be made on this
  // endpoint.
  //
  // It is not allowed to call |Bind| on an initialized client. To rebind a
  // |WireSyncClient| to a different endpoint, simply replace the
  // |WireSyncClient| variable with a new instance.
  void Bind(fdf::ClientEnd<FidlProtocol> client_end) {
    ZX_ASSERT(!is_valid());
    client_end_ = std::move(client_end);
    ZX_ASSERT(is_valid());
  }

  // Extracts the underlying endpoint from the client. After this operation, the
  // client goes back to an uninitialized state.
  //
  // It is not safe to invoke this method while there are ongoing FIDL calls.
  fdf::ClientEnd<FidlProtocol> TakeClientEnd() {
    ZX_ASSERT(is_valid());
    return std::move(client_end_);
  }

  // Returns an interface for making FIDL calls, using the provided |arena| to
  // allocate buffers necessary for each call. Requests will live on the arena.
  // Responses on the other hand live on the arena passed along with the
  // response, which may or may not be the same arena as the request.
  auto buffer(const fdf::Arena& arena) const {
    ZX_ASSERT(is_valid());
    return fdf::internal::SyncEndpointBufferVeneer<
        fidl::internal::WireSyncBufferClientImpl<FidlProtocol>>(
        fidl::internal::MakeAnyUnownedTransport(client_end_.handle()), arena);
  }

 private:
  fdf::ClientEnd<FidlProtocol> client_end_;
};

template <typename FidlProtocol>
WireSyncClient(ClientEnd<FidlProtocol>) -> WireSyncClient<FidlProtocol>;

// |WireCall| is used to make method calls directly on a |fdf::ClientEnd|
// without having to set up a client. Call it like:
//
//     fdf::Arena arena('EXAM');
//     fdf::WireCall(client_end).buffer(arena)->Method(args...);
//
template <typename FidlProtocol>
fdf::internal::SyncEndpointVeneer<FidlProtocol> WireCall(
    const ClientEnd<FidlProtocol>& client_end) {
  return fdf::internal::SyncEndpointVeneer<FidlProtocol>(
      fidl::internal::MakeAnyUnownedTransport(client_end.borrow().handle()));
}

// |WireCall| is used to make method calls directly on a |fdf::ClientEnd|
// without having to set up a client. Call it like:
//
//     fdf::Arena arena('EXAM');
//     fdf::WireCall(client_end).buffer(arena)->Method(args...);
//
template <typename FidlProtocol>
fdf::internal::SyncEndpointVeneer<FidlProtocol> WireCall(
    const UnownedClientEnd<FidlProtocol>& client_end) {
  return fdf::internal::SyncEndpointVeneer<FidlProtocol>(
      fidl::internal::MakeAnyUnownedTransport(client_end.handle()));
}
}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SYNC_CALL_H_
