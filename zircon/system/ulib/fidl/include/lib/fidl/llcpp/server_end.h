// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_END_H_
#define LIB_FIDL_LLCPP_SERVER_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/soft_migration.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <zircon/assert.h>

namespace fidl {

// The server endpoint of a FIDL channel.
//
// The remote (client) counterpart of the channel expects this end of the
// channel to serve the protocol represented by |Protocol|. This type is the
// dual of |ClientEnd|.
//
// |ServerEnd| is thread-compatible: the caller should not use the underlying
// channel (e.g. sending an event) while the server-end object is being mutated
// in a different thread.
template <typename Protocol>
class ServerEnd final {
 public:
  using ProtocolType = Protocol;

  // Creates a |ServerEnd| whose underlying channel is invalid.
  //
  // Both optional and non-optional server endpoints in FIDL declarations map
  // to this same type. If this ServerEnd is passed to a method or FIDL
  // protocol that requires valid channels, those operations will fail at
  // run-time.
  ServerEnd() = default;

  // Creates an |ServerEnd| that wraps the given |channel|.
  // The caller must ensure the |channel| is a server endpoint speaking
  // a protocol compatible with |Protocol|.
  // TODO(fxbug.dev/65212): Make the conversion explicit as users migrate to
  // typed channels.
  // NOLINTNEXTLINE
  FIDL_CONDITIONALLY_EXPLICIT_CONVERSION ServerEnd(zx::channel channel)
      : channel_(std::move(channel)) {}

  ServerEnd(ServerEnd&& other) noexcept = default;
  ServerEnd& operator=(ServerEnd&& other) noexcept = default;

  // Whether the underlying channel is valid.
  bool is_valid() const { return channel_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // Close the underlying channel if any,
  // and reset the object back to an invalid state.
  void reset() { channel_.reset(); }

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }
  zx::channel& channel() { return channel_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(channel_); }

  zx::channel TakeTransportObject() { return TakeChannel(); }

  // Sends an epitaph over the underlying channel, then closes the channel.
  // An epitaph is a final optional message sent over a server-end towards
  // the client, before the server-end is closed down. See the FIDL
  // language spec for more information about epitaphs.
  //
  // The server-end must be holding a valid underlying channel.
  // Returns the status of the channel write operation.
  zx_status_t Close(zx_status_t epitaph_value) {
    if (!is_valid()) {
      ZX_PANIC("Cannot close an invalid ServerEnd.");
    }
    zx::channel channel = TakeChannel();
    return fidl_epitaph_write(channel.get(), epitaph_value);
  }

 private:
  zx::channel channel_;
};

template <typename Protocol>
class SocketServerEnd final {
 public:
  using ProtocolType = Protocol;

  // Creates a |ServerEnd| whose underlying channel is invalid.
  //
  // Both optional and non-optional server endpoints in FIDL declarations map
  // to this same type. If this ServerEnd is passed to a method or FIDL
  // protocol that requires valid channels, those operations will fail at
  // run-time.
  SocketServerEnd() = default;

  // Creates an |ServerEnd| that wraps the given |channel|.
  // The caller must ensure the |channel| is a server endpoint speaking
  // a protocol compatible with |Protocol|.
  // TODO(fxbug.dev/65212): Make the conversion explicit as users migrate to
  // typed channels.
  // NOLINTNEXTLINE
  FIDL_CONDITIONALLY_EXPLICIT_CONVERSION SocketServerEnd(zx::socket socket)
      : socket_(std::move(socket)) {}

  SocketServerEnd(SocketServerEnd&& other) noexcept = default;
  SocketServerEnd& operator=(SocketServerEnd&& other) noexcept = default;

  // Whether the underlying channel is valid.
  bool is_valid() const { return socket_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // Close the underlying channel if any,
  // and reset the object back to an invalid state.
  void reset() { socket_.reset(); }

  zx::socket TakeTransportObject() { return std::move(socket_); }

  // Sends an epitaph over the underlying channel, then closes the channel.
  // An epitaph is a final optional message sent over a server-end towards
  // the client, before the server-end is closed down. See the FIDL
  // language spec for more information about epitaphs.
  //
  // The server-end must be holding a valid underlying channel.
  // Returns the status of the channel write operation.
  zx_status_t Close(zx_status_t epitaph_value) {
    if (!is_valid()) {
      ZX_PANIC("Cannot close an invalid ServerEnd.");
    }
    zx::socket socket = TakeTransportObject();
    return fidl_epitaph_write(socket.get(), epitaph_value);
  }

 private:
  zx::socket socket_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_END_H_
