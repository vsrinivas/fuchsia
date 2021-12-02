// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_END_H_
#define LIB_FIDL_LLCPP_CLIENT_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/internal/transport_end.h>
#include <lib/fidl/llcpp/soft_migration.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <utility>

namespace fidl {
namespace internal {

template <typename Protocol, typename Transport>
class ClientEndBase : public TransportEnd<Protocol, Transport> {
  using TransportEnd = TransportEnd<Protocol, Transport>;

 public:
  using TransportEnd::TransportEnd;

  // Returns a type-safe copy of the underlying handle in this |ClientEndBase|
  // that does not claim ownership.
  UnownedClientEnd<Protocol, Transport> borrow() const {
    return UnownedClientEnd<Protocol, Transport>(TransportEnd::handle_.borrow());
  }
};

template <typename Protocol, typename Transport>
class UnownedClientEndBase : public UnownedTransportEnd<Protocol, Transport> {
  using UnownedTransportEnd = UnownedTransportEnd<Protocol, Transport>;

 public:
  using UnownedTransportEnd::UnownedTransportEnd;

  // An unowned client end can only be constructed from an existing handle.
  //
  // This constructor defines an implicit conversion to facilitate invoking
  // generated FIDL APIs with either an unowned client end, or a const
  // reference to a |TransportEndSubclass|.
  // NOLINTNEXTLINE
  UnownedClientEndBase(const ClientEnd<Protocol, Transport>& owner)
      : UnownedClientEndBase(owner.handle()->get()) {}
};

}  // namespace internal

// The client endpoint of a FIDL channel.
//
// The remote (server) counterpart of the channel expects this end of the
// channel to speak the protocol represented by |Protocol|. This type is the
// dual of |ServerEnd|.
//
// |ClientEnd| is thread-compatible: it may be transferred to another thread
// or another process.
template <typename Protocol>
class ClientEnd<Protocol, internal::ChannelTransport> final
    : public internal::ClientEndBase<Protocol, internal::ChannelTransport> {
  using ClientEndBase = internal::ClientEndBase<Protocol, internal::ChannelTransport>;

 public:
  using ClientEndBase::ClientEndBase;

  // The underlying channel.
  const zx::channel& channel() const { return ClientEndBase::handle_; }
  zx::channel& channel() { return ClientEndBase::handle_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(ClientEndBase::handle_); }
};

// A typed client endpoint that does not claim ownership. It is typically
// created from an owning |fidl::ClientEnd<Protocol>|.
// These types are used by generated FIDL APIs that do not take ownership.
//
// The remote (server) counterpart of the channel expects this end of the
// channel to speak the protocol represented by |Protocol|.
//
// Compared to a |const fidl::ClientEnd<Protocol>&|,
// |fidl::UnownedClientEnd<Protocol>| has the additional flexibility of being
// able to be stored in a member variable or field, while still remembering
// the associated FIDL protocol.
template <typename Protocol, typename Transport>
class UnownedClientEnd final : public internal::UnownedClientEndBase<Protocol, Transport> {
  using UnownedClientEndBase = internal::UnownedClientEndBase<Protocol, Transport>;

 public:
  using UnownedClientEndBase::UnownedClientEndBase;

  zx::unowned_channel channel() const { return zx::unowned_channel(UnownedClientEndBase::handle_); }
};

// Comparison operators between client-end objects.
// For the channel transport, these comparisons have the same semantics
// as the comparison operators on the wrapped |zx::channel|s.

template <typename T, typename U>
bool operator==(const ClientEnd<T, U>& a, const ClientEnd<T, U>& b) {
  return a.handle() == b.handle();
}

template <typename T, typename U>
bool operator!=(const ClientEnd<T, U>& a, const ClientEnd<T, U>& b) {
  return !(a == b);
}

template <typename T, typename U>
bool operator<(const ClientEnd<T, U>& a, const ClientEnd<T, U>& b) {
  return a.handle() < b.handle();
}

template <typename T, typename U>
bool operator>(const ClientEnd<T, U>& a, const ClientEnd<T, U>& b) {
  return a.handle() > b.handle();
}

template <typename T, typename U>
bool operator<=(const ClientEnd<T, U>& a, const ClientEnd<T, U>& b) {
  return a.handle() <= b.handle();
}

template <typename T, typename U>
bool operator>=(const ClientEnd<T, U>& a, const ClientEnd<T, U>& b) {
  return a.handle() >= b.handle();
}

template <typename T, typename U>
bool operator==(const UnownedClientEnd<T, U>& a, const UnownedClientEnd<T, U>& b) {
  return a.handle() == b.handle();
}

template <typename T, typename U>
bool operator!=(const UnownedClientEnd<T, U>& a, const UnownedClientEnd<T, U>& b) {
  return !(a == b);
}

template <typename T, typename U>
bool operator<(const UnownedClientEnd<T, U>& a, const UnownedClientEnd<T, U>& b) {
  return a.handle() < b.handle();
}

template <typename T, typename U>
bool operator>(const UnownedClientEnd<T, U>& a, const UnownedClientEnd<T, U>& b) {
  return a.handle() > b.handle();
}

template <typename T, typename U>
bool operator<=(const UnownedClientEnd<T, U>& a, const UnownedClientEnd<T, U>& b) {
  return a.handle() <= b.handle();
}

template <typename T, typename U>
bool operator>=(const UnownedClientEnd<T, U>& a, const UnownedClientEnd<T, U>& b) {
  return a.handle() >= b.handle();
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_END_H_
