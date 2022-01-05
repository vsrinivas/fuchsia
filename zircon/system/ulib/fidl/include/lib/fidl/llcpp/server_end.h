// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_END_H_
#define LIB_FIDL_LLCPP_SERVER_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/internal/transport.h>
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

template <typename Protocol, typename Transport>
class ServerEndImpl;

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
class ServerEnd final : public internal::ServerEndImpl<Protocol, typename Protocol::Transport> {
 public:
  using internal::ServerEndImpl<Protocol, typename Protocol::Transport>::ServerEndImpl;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_END_H_
