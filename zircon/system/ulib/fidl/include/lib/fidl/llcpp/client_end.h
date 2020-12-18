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

 private:
  zx::channel channel_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_END_H_
