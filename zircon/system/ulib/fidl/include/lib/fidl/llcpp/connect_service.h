// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CONNECT_SERVICE_H_
#define LIB_FIDL_LLCPP_CONNECT_SERVICE_H_

#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fpromise/result.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#endif  // __Fuchsia__

namespace fidl {

#ifdef __Fuchsia__

// Creates a synchronous FIDL client for the FIDL protocol `FidlProtocol`, bound to the
// given channel.
template <typename FidlProtocol>
typename fidl::WireSyncClient<FidlProtocol> BindSyncClient(ClientEnd<FidlProtocol> client_end) {
  return typename fidl::WireSyncClient<FidlProtocol>(std::move(client_end));
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

// Creates a pair of Zircon channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |zx::channel::create|, because this method encodes the precise protocol type
// into its results at compile time.
//
// This overload of |CreateEndpoints| may lead to more concise code when the
// caller already has the client endpoint defined as an instance variable.
// It will replace the destination of |out_client| with a newly created client
// endpoint, and return the corresponding server endpoint in a |zx::status|:
//
//     // |client_end_| is an instance variable.
//     auto server_end = fidl::CreateEndpoints(&client_end_);
//     if (server_end.is_ok()) { ... }
template <typename Protocol>
zx::status<fidl::ServerEnd<Protocol>> CreateEndpoints(fidl::ClientEnd<Protocol>* out_client) {
  auto endpoints = fidl::CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_client = fidl::ClientEnd<Protocol>(std::move(endpoints->client));
  return zx::ok(fidl::ServerEnd<Protocol>(std::move(endpoints->server)));
}

// Creates a pair of Zircon channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |zx::channel::create|, because this method encodes the precise protocol type
// into its results at compile time.
//
// This overload of |CreateEndpoints| may lead to more concise code when the
// caller already has the server endpoint defined as an instance variable.
// It will replace the destination of |out_server| with a newly created server
// endpoint, and return the corresponding client endpoint in a |zx::status|:
//
//     // |server_end_| is an instance variable.
//     auto client_end = fidl::CreateEndpoints(&server_end_);
//     if (client_end.is_ok()) { ... }
template <typename Protocol>
zx::status<fidl::ClientEnd<Protocol>> CreateEndpoints(fidl::ServerEnd<Protocol>* out_server) {
  auto endpoints = fidl::CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_server = fidl::ServerEnd<Protocol>(std::move(endpoints->server));
  return zx::ok(fidl::ClientEnd<Protocol>(std::move(endpoints->client)));
}

namespace internal {

// The method signature required to implement the method that issues the Directory::Open
// FIDL call for a Service's member protocol.
using ConnectMemberFunc = zx::status<> (*)(zx::unowned_channel service_dir,
                                           fidl::StringView member_name, zx::channel channel);

}  // namespace internal

#endif  // __Fuchsia__

namespace internal {
// This struct template is specialized in generated bindings to include the following
// protocol-specific members:
//  - static constexpr char DiscoverableName[] - the discoverable name if any exists.
template <typename Protocol>
struct ProtocolDetails;

// Helper type for compile-time string concatenation.
template <const char*, typename>
struct default_service_path;
template <const char* n, size_t... i>
struct default_service_path<n, std::integer_sequence<size_t, i...>> {
  static constexpr const char value[]{'/', 's', 'v', 'c', '/', n[i]...};
};
}  // namespace internal

// DiscoverableProtocolName<Protocol> evaluates to a string containing the name of the protocol,
// including its library.
template <typename Protocol>
constexpr const char* DiscoverableProtocolName =
    fidl::internal::ProtocolDetails<Protocol>::DiscoverableName;

// DiscoverableProtocolDefaultPath<Protocol> evaluates to a string containing the default path for
// the protocol endpoint, something like "/svc/fuchsia.library.Protocol".
template <typename Protocol>
constexpr const char* DiscoverableProtocolDefaultPath = fidl::internal::default_service_path<
    fidl::internal::ProtocolDetails<Protocol>::DiscoverableName,
    std::make_integer_sequence<
        size_t, sizeof(fidl::internal::ProtocolDetails<Protocol>::DiscoverableName)>>::value;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CONNECT_SERVICE_H_
