// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/fidl/llcpp/internal/transport.h>

namespace fdf {
template <typename Protocol>
class ClientEnd;
template <typename Protocol>
class UnownedClientEnd;
template <typename Protocol>
class ServerEnd;
template <typename Protocol>
class ServerBindingRef;
template <typename FidlMethod>
class WireUnownedResult;
}  // namespace fdf

namespace fidl {
namespace internal {

struct DriverHandleMetadata {};

struct DriverTransport {
  using OwnedType = fdf::Channel;
  using UnownedType = fdf::UnownedChannel;
  using HandleMetadata = DriverHandleMetadata;
  using IncomingTransportContextType = fdf_arena_t;
  using OutgoingTransportContextType = fdf_arena_t;
  template <typename Protocol>
  using ClientEnd = fdf::ClientEnd<Protocol>;
  template <typename Protocol>
  using UnownedClientEnd = fdf::UnownedClientEnd<Protocol>;
  template <typename Protocol>
  using ServerEnd = fdf::ServerEnd<Protocol>;
  template <typename Protocol>
  using ServerBindingRef = fdf::ServerBindingRef<Protocol>;
  template <typename FidlMethod>
  using WireUnownedResult = fdf::WireUnownedResult<FidlMethod>;

  static constexpr bool TransportProvidesReadBuffer = true;

  static const TransportVTable VTable;
  static const CodingConfig EncodingConfiguration;
};

template <>
struct AssociatedTransportImpl<fdf::Channel> {
  using type = DriverTransport;
};
template <>
struct AssociatedTransportImpl<fdf::UnownedChannel> {
  using type = DriverTransport;
};
template <>
struct AssociatedTransportImpl<DriverHandleMetadata> {
  using type = DriverTransport;
};

class DriverWaiter : public TransportWaiter {
 public:
  DriverWaiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
               TransportWaitSuccessHandler success_handler,
               TransportWaitFailureHandler failure_handler) {
    state_.handle = handle;
    state_.dispatcher = dispatcher;
    state_.success_handler = std::move(success_handler);
    state_.failure_handler = std::move(failure_handler);
  }

  zx_status_t Begin() override;

  zx_status_t Cancel() override;

 private:
  struct State {
    fidl_handle_t handle;
    async_dispatcher_t* dispatcher;
    TransportWaitSuccessHandler success_handler;
    TransportWaitFailureHandler failure_handler;
    std::optional<fdf::ChannelRead> channel_read;
  };
  State state_;
};

}  // namespace internal
}  // namespace fidl

namespace fdf {
template <typename Protocol>
class ClientEnd final
    : public fidl::internal::ClientEndBase<Protocol, fidl::internal::DriverTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::DriverTransport>);

 public:
  using fidl::internal::ClientEndBase<Protocol, fidl::internal::DriverTransport>::ClientEndBase;
};

template <typename Protocol>
class UnownedClientEnd final
    : public fidl::internal::UnownedClientEndBase<Protocol, fidl::internal::DriverTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::DriverTransport>);

 public:
  using fidl::internal::UnownedClientEndBase<Protocol,
                                             fidl::internal::DriverTransport>::UnownedClientEndBase;
};

template <typename Protocol>
class ServerEnd final
    : public fidl::internal::ServerEndBase<Protocol, fidl::internal::DriverTransport> {
  static_assert(cpp17::is_same_v<typename Protocol::Transport, fidl::internal::DriverTransport>);

 public:
  using fidl::internal::ServerEndBase<Protocol, fidl::internal::DriverTransport>::ServerEndBase;
};

template <typename Protocol>
struct Endpoints {
  fdf::ClientEnd<Protocol> client;
  fdf::ServerEnd<Protocol> server;
};

// Creates a pair of fdf channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |fdf::ChannelPair::Create|, because this method encodes the precise protocol
// type into its results at compile time.
//
// The return value is a result type wrapping the client and server endpoints.
// Given the following:
//
//     auto endpoints = fdf::CreateEndpoints<MyProtocol>();
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
zx::status<fdf::Endpoints<Protocol>> CreateEndpoints() {
  auto pair = fdf::ChannelPair::Create(0);
  if (!pair.is_ok()) {
    return pair.take_error();
  }
  return zx::ok(Endpoints<Protocol>{
      fdf::ClientEnd<Protocol>(std::move(pair->end0)),
      fdf::ServerEnd<Protocol>(std::move(pair->end1)),
  });
}

// Creates a pair of fdf channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |fdf::ChannelPair::Create|, because this method encodes the precise protocol
// type into its results at compile time.
//
// This overload of |CreateEndpoints| may lead to more concise code when the
// caller already has the client endpoint defined as an instance variable.
// It will replace the destination of |out_client| with a newly created client
// endpoint, and return the corresponding server endpoint in a |zx::status|:
//
//     // |client_end_| is an instance variable.
//     auto server_end = fdf::CreateEndpoints(&client_end_);
//     if (server_end.is_ok()) { ... }
template <typename Protocol>
zx::status<fdf::ServerEnd<Protocol>> CreateEndpoints(fdf::ClientEnd<Protocol>* out_client) {
  auto endpoints = CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_client = fdf::ClientEnd<Protocol>(std::move(endpoints->client));
  return zx::ok(fdf::ServerEnd<Protocol>(std::move(endpoints->server)));
}

// Creates a pair of fdf channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |fdf::ChannelPair::Create|, because this method encodes the precise protocol
// type into its results at compile time.
//
// This overload of |CreateEndpoints| may lead to more concise code when the
// caller already has the server endpoint defined as an instance variable.
// It will replace the destination of |out_server| with a newly created server
// endpoint, and return the corresponding client endpoint in a |zx::status|:
//
//     // |server_end_| is an instance variable.
//     auto client_end = fdf::CreateEndpoints(&server_end_);
//     if (client_end.is_ok()) { ... }
template <typename Protocol>
zx::status<fdf::ClientEnd<Protocol>> CreateEndpoints(fdf::ServerEnd<Protocol>* out_server) {
  auto endpoints = CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_server = fdf::ServerEnd<Protocol>(std::move(endpoints->server));
  return zx::ok(fdf::ClientEnd<Protocol>(std::move(endpoints->client)));
}

}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_
