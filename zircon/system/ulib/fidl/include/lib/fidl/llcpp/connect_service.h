// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CONNECT_SERVICE_H_
#define LIB_FIDL_LLCPP_CONNECT_SERVICE_H_

#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fit/result.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#endif  // __Fuchsia__

namespace fidl {

// A FIDL-specific result type. Alias of fit::result<T, E> for FIDL result types, whose error (E) is
// always zx_status_t.
template <typename T = void>
using result = fit::result<T, zx_status_t>;

#ifdef __Fuchsia__

// TODO(fxbug.dev/65212): ClientChannel may be replaced by fidl::ClientEnd.
// A wrapper around a Zircon channel, strongly-typed on a FIDL protocol.
template <typename FidlProtocol>
class ClientChannel final {
 public:
  explicit ClientChannel(zx::channel channel) : channel_(std::move(channel)) {}

  // Moves the underlying Zircon channel out to the caller. Once called, this object
  // should no longer be used.
  zx::channel take_channel() { return std::move(channel_); }

  // Returns a reference to the underlying Zircon channel.
  const zx::channel& channel() const { return channel_; }

 private:
  zx::channel channel_;
};

// Creates a synchronous FIDL client for the FIDL protocol `FidlProtocol`, bound to the
// given channel.
template <typename FidlProtocol>
typename FidlProtocol::SyncClient BindSyncClient(ClientChannel<FidlProtocol> channel) {
  return typename FidlProtocol::SyncClient(channel.take_channel());
}

template <typename Protocol>
struct Endpoints {
  fidl::ClientEnd<Protocol> client;
  fidl::ServerEnd<Protocol> server;
};

// Creates a pair of Zircon channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |zx::channel::create|, because this method encodes the precise protocol type
// into its results at compile time.
//
// The return value is a result type wrapping the client and server endpoints.
// Given the following:
//
//     auto endpoints = fidl::CreateEndpoints<MyProtocol>();
//
// The caller should first ensure that |endpoints.is_ok() == true|, after which
// the channel endpoints may be accessed in one of two ways:
//
// - Direct:
//     endpoints->client;
//     endpoints->server;
//
// - Structured Binding:
//     auto [client_end, server_end] = std::move(endpoints.value());
template <typename Protocol>
zx::status<Endpoints<Protocol>> CreateEndpoints() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error_status(status);
  }
  return zx::ok(Endpoints<Protocol>{
      fidl::ClientEnd<Protocol>(std::move(local)),
      fidl::ServerEnd<Protocol>(std::move(remote)),
  });
}

namespace internal {

// The method signature required to implement the method that issues the Directory::Open
// FIDL call for a Service's member protocol.
using ConnectMemberFunc = zx_status_t (*)(zx::unowned_channel service_dir,
                                          fidl::StringView member_name, zx::channel channel);

}  // namespace internal

#endif  // __Fuchsia__

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CONNECT_SERVICE_H_
