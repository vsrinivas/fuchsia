// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_END_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_END_H_

#include <lib/fidl/llcpp/soft_migration.h>
#include <zircon/fidl.h>

#include <utility>

namespace fidl {
namespace internal {

// Base class for the owned client or server endpoint of a FIDL handle.
template <typename Protocol, typename Transport>
class TransportEnd {
  using OwnedType = typename Transport::OwnedType;
  using UnownedType = typename Transport::UnownedType;

 public:
  using ProtocolType = Protocol;

  // Creates a |TransportEnd| whose underlying handle is invalid.
  //
  // Both optional and non-optional endpoints in FIDL declarations map
  // to this same type. If this |TransportEnd| is passed to a method or FIDL
  // protocol that requires valid handles, those operations will fail at
  // run-time.
  TransportEnd() = default;

  // Creates an |TransportEnd| that wraps the given |handle|.
  // The caller must ensure the |handle| is a server endpoint speaking
  // a protocol compatible with |Protocol|.
  // TODO(fxbug.dev/65212): Make the conversion explicit as users migrate to
  // typed handles.
  // NOLINTNEXTLINE
  FIDL_CONDITIONALLY_EXPLICIT_CONVERSION TransportEnd(OwnedType handle)
      : handle_(std::move(handle)) {}

  TransportEnd(TransportEnd&& other) noexcept = default;
  TransportEnd& operator=(TransportEnd&& other) noexcept = default;

  // Whether the underlying handle is valid.
  bool is_valid() const { return handle_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // Close the underlying handle if any,
  // and reset the object back to an invalid state.
  void reset() { handle_.reset(); }

  // The underlying handle.
  UnownedType handle() const { return handle_.borrow(); }

  // Transfers ownership of the underlying handle to the caller.
  OwnedType TakeHandle() { return std::move(handle_); }

 protected:
  OwnedType handle_;
};

// Base class for the unowned client or server endpoint of a FIDL handle.
template <typename Protocol, typename Transport>
class UnownedTransportEnd {
  using UnownedType = typename Transport::UnownedType;

 public:
  using ProtocolType = Protocol;

  // Creates an |UnownedTransportEnd| from a raw handle.
  // Prefer only using this constructor when interfacing with C APIs.
  constexpr explicit UnownedTransportEnd(fidl_handle_t h) : handle_(h) {}

  // Creates an |UnownedTransportEnd|. In the case of the channel transport, it
  // will construct the |UnownedTransportEnd| from a |zx::unowned_channel|.
  //
  // Using this constructor is discouraged since it tends to erase the actual
  // type of the underlying protocol.
  // Consider declaring the type of the input variable as a
  // |fidl::UnownedTransportEnd<Protocol, Transport>| instead.
  //
  // TODO(fxbug.dev/65212): Make the conversion explicit as users migrate to
  // typed channels.
  // NOLINTNEXTLINE
  FIDL_CONDITIONALLY_EXPLICIT_CONVERSION UnownedTransportEnd(const UnownedType& h)
      : handle_(h->get()) {}

  // The unowned transport end is copyable - it simply copies the handle value.
  UnownedTransportEnd(const UnownedTransportEnd& other) = default;
  UnownedTransportEnd& operator=(const UnownedTransportEnd& other) = default;

  // Whether the underlying handle is valid.
  bool is_valid() const { return handle_ != FIDL_HANDLE_INVALID; }
  explicit operator bool() const { return is_valid(); }

  // The underlying handle.
  UnownedType handle() const { return UnownedType(handle_); }

 protected:
  fidl_handle_t handle_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_END_H_
