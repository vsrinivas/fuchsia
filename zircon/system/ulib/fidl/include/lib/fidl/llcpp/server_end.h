// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_END_H_
#define LIB_FIDL_LLCPP_SERVER_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/internal/transport_end.h>
#include <lib/fidl/llcpp/soft_migration.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

namespace fidl {
namespace internal {

template <typename Protocol, typename Transport>
class ServerEndBase : public TransportEnd<Protocol, Transport> {
  using TransportEnd = TransportEnd<Protocol, Transport>;

 public:
  using TransportEnd::TransportEnd;
};

}  // namespace internal

// The server endpoint of a FIDL handle.
//
// The remote (client) counterpart of the handle expects this end of the
// handle to serve the protocol represented by |Protocol|. This type is the
// dual of |ClientEnd|.
//
// |ServerEnd| is thread-compatible: the caller should not use the underlying
// handle (e.g. sending an event) while the server-end object is being mutated
// in a different thread.
template <typename Protocol>
class ServerEnd<Protocol, internal::ChannelTransport> final
    : public internal::ServerEndBase<Protocol, internal::ChannelTransport> {
  using ServerEndBase = internal::ServerEndBase<Protocol, internal::ChannelTransport>;

 public:
  using ServerEndBase::ServerEndBase;

  const zx::channel& channel() const { return ServerEndBase::handle_; }
  zx::channel& channel() { return ServerEndBase::handle_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return ServerEndBase::TakeHandle(); }

  // Sends an epitaph over the underlying channel, then closes the channel.
  // An epitaph is a final optional message sent over a server-end towards
  // the client, before the server-end is closed down. See the FIDL
  // language spec for more information about epitaphs.
  //
  // The server-end must be holding a valid underlying channel.
  // Returns the status of the channel write operation.
  zx_status_t Close(zx_status_t epitaph_value) {
    if (!ServerEndBase::is_valid()) {
      ZX_PANIC("Cannot close an invalid ServerEnd.");
    }
    zx::channel channel = TakeChannel();
    return fidl_epitaph_write(channel.get(), epitaph_value);
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_END_H_
