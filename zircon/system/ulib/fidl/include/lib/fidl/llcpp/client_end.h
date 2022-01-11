// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_END_H_
#define LIB_FIDL_LLCPP_CLIENT_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/internal/transport.h>
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
  UnownedClientEnd<Protocol> borrow() const {
    return UnownedClientEnd<Protocol>(TransportEnd::handle_.borrow());
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
  UnownedClientEndBase(const ClientEndType<Protocol>& owner)
      : UnownedClientEndBase(owner.handle()->get()) {}
};

// Comparison operators between client-end objects.
// For the channel transport, these comparisons have the same semantics
// as the comparison operators on the wrapped |zx::channel|s.

template <typename T, typename U>
bool operator==(const ClientEndBase<T, U>& a, const ClientEndBase<T, U>& b) {
  return a.handle() == b.handle();
}

template <typename T, typename U>
bool operator!=(const ClientEndBase<T, U>& a, const ClientEndBase<T, U>& b) {
  return !(a == b);
}

template <typename T, typename U>
bool operator<(const ClientEndBase<T, U>& a, const ClientEndBase<T, U>& b) {
  return a.handle() < b.handle();
}

template <typename T, typename U>
bool operator>(const ClientEndBase<T, U>& a, const ClientEndBase<T, U>& b) {
  return a.handle() > b.handle();
}

template <typename T, typename U>
bool operator<=(const ClientEndBase<T, U>& a, const ClientEndBase<T, U>& b) {
  return a.handle() <= b.handle();
}

template <typename T, typename U>
bool operator>=(const ClientEndBase<T, U>& a, const ClientEndBase<T, U>& b) {
  return a.handle() >= b.handle();
}

template <typename T, typename U>
bool operator==(const UnownedClientEndBase<T, U>& a, const UnownedClientEndBase<T, U>& b) {
  return a.handle() == b.handle();
}

template <typename T, typename U>
bool operator!=(const UnownedClientEndBase<T, U>& a, const UnownedClientEndBase<T, U>& b) {
  return !(a == b);
}

template <typename T, typename U>
bool operator<(const UnownedClientEndBase<T, U>& a, const UnownedClientEndBase<T, U>& b) {
  return a.handle() < b.handle();
}

template <typename T, typename U>
bool operator>(const UnownedClientEndBase<T, U>& a, const UnownedClientEndBase<T, U>& b) {
  return a.handle() > b.handle();
}

template <typename T, typename U>
bool operator<=(const UnownedClientEndBase<T, U>& a, const UnownedClientEndBase<T, U>& b) {
  return a.handle() <= b.handle();
}

template <typename T, typename U>
bool operator>=(const UnownedClientEndBase<T, U>& a, const UnownedClientEndBase<T, U>& b) {
  return a.handle() >= b.handle();
}

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_END_H_
