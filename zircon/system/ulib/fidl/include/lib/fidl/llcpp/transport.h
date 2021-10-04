// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TRANSPORT_H_
#define LIB_FIDL_LLCPP_TRANSPORT_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>

namespace fidl {

template <typename Protocol>
struct ChannelTransport {
  using TransportObject = zx::channel;
  using ServerEnd = typename ::fidl::ServerEnd<Protocol>;

  static zx::channel TakeTransportObject(::fidl::ServerEnd<Protocol>& s) {
      return s.TakeChannel();
  }
};

// Temp
template <typename Protocol>
using FakeDDKTransport = ChannelTransport<Protocol>;

/*
template <typename Protocol>
struct SocketTransport {
  using TransportObject = zx::socket;
  using ServerEnd = ::fidl::SocketServerEnd<Protocol>;
};*/

template <typename Protocol>
using SocketTransport = ChannelTransport<Protocol>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_TRANSPORT_H_
