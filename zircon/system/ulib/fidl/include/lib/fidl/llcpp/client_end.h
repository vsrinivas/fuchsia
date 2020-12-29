// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_END_H_
#define LIB_FIDL_LLCPP_CLIENT_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fit/optional.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

namespace fidl {

template <typename Protocol>
class UnownedClientEnd;

// The client endpoint of a FIDL channel.
//
// The remote (server) counterpart of the channel expects this end of the
// channel to speak the protocol represented by |Protocol|. This type is the
// dual of |ServerEnd|.
//
// |ClientEnd| is thread-compatible: it may be transferred to another thread
// or another process.
template <typename Protocol>
class ClientEnd final {
 public:
  // Creates a |ClientEnd| whose underlying channel is invalid.
  //
  // Both optional and non-optional client endpoints in FIDL declarations map
  // to this same type. If this ClientEnd is passed to a method or FIDL
  // protocol that requires valid channels, those operations will fail at
  // run-time.
  ClientEnd() = default;

  // Creates an |ClientEnd| that wraps the given |channel|.
  // The caller must ensure the |channel| is a client endpoint speaking
  // a protocol compatible with |Protocol|.
  explicit ClientEnd(zx::channel channel) : channel_(std::move(channel)) {}

  ClientEnd(ClientEnd&& other) noexcept = default;
  ClientEnd& operator=(ClientEnd&& other) noexcept = default;

  // Whether the underlying channel is valid.
  bool is_valid() const { return channel_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }
  zx::channel& channel() { return channel_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(channel_); }

  // Returns a type-safe copy of the underlying channel in this |ClientEnd|
  // that does not claim ownership.
  fidl::UnownedClientEnd<Protocol> borrow() const {
    return UnownedClientEnd<Protocol>(channel_.borrow());
  }

 private:
  zx::channel channel_;
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
template <typename Protocol>
class UnownedClientEnd final {
 public:
  // An unowned client end can only be constructed from an existing channel.
  //
  // This constructor defines an implicit conversion to facilitate invoking
  // generated FIDL APIs with either an unowned client end, or a const
  // reference to a |fidl::ClientEnd<Protocol>|.
  // NOLINTNEXTLINE
  UnownedClientEnd(const ClientEnd<Protocol>& owner) : UnownedClientEnd(owner.channel().get()) {}

  // Creates an |UnownedClientEnd| from a raw zircon handle.
  // Prefer only using this constructor when interfacing with C APIs.
  explicit UnownedClientEnd(zx_handle_t h) : channel_(h) {}

  // Creates an |UnownedClientEnd| from a |zx::unowned_channel|.
  //
  // Using this constructor is discouraged since it tends to erase the actual
  // type of the underlying protocol.
  // Consider declaring the type of the input variable as a
  // |fidl::UnownedClientEnd<Protocol>| instead.
  //
  // TODO(fxbug.dev/65212): Make the conversion explicit as users migrate to
  // typed channels.
  // NOLINTNEXTLINE
  UnownedClientEnd(const zx::unowned_channel& h) : channel_(h->get()) {}

  // The unowned client end is copyable - it simply copies the handle value.
  UnownedClientEnd(const UnownedClientEnd& other) = default;
  UnownedClientEnd& operator=(const UnownedClientEnd& other) = default;

  // Whether the underlying channel is valid.
  bool is_valid() const { return channel_ != ZX_HANDLE_INVALID; }
  explicit operator bool() const { return is_valid(); }

  // The underlying channel.
  zx_handle_t channel() const { return channel_; }

 private:
  zx_handle_t channel_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_END_H_
