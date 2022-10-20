// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_

#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/internal/transport.h>

namespace fdf {
template <typename Protocol>
class ClientEnd;
template <typename Protocol>
class UnownedClientEnd;
template <typename Protocol>
class ServerEnd;
template <typename Protocol>
class UnownedServerEnd;
template <typename Protocol>
class ServerBindingRef;
template <typename FidlMethod>
class WireUnownedResult;
template <typename FidlMethod>
class Result;
}  // namespace fdf

namespace fidl {

template <>
struct ContainsHandle<::fdf::Channel> : std::true_type {};
template <typename T>
struct ContainsHandle<::fdf::ClientEnd<T>> : std::true_type {};
template <typename T>
struct ContainsHandle<::fdf::ServerEnd<T>> : std::true_type {};

namespace internal {

struct DriverHandleMetadata {};

struct DriverMessageStorageView;

struct DriverTransport {
  using OwnedType = fdf::Channel;
  using UnownedType = fdf::UnownedChannel;
  using HandleMetadata = DriverHandleMetadata;
  using OutgoingTransportContextType = fdf_arena_t;
  template <typename Protocol>
  using ClientEnd = fdf::ClientEnd<Protocol>;
  template <typename Protocol>
  using UnownedClientEnd = fdf::UnownedClientEnd<Protocol>;
  template <typename Protocol>
  using ServerEnd = fdf::ServerEnd<Protocol>;
  template <typename Protocol>
  using UnownedServerEnd = fdf::UnownedServerEnd<Protocol>;
  template <typename Protocol>
  using ServerBindingRef = fdf::ServerBindingRef<Protocol>;
  template <typename FidlMethod>
  using WireUnownedResult = fdf::WireUnownedResult<FidlMethod>;
  template <typename FidlMethod>
  using Result = fdf::Result<FidlMethod>;
  using MessageStorageView = DriverMessageStorageView;

  static constexpr bool kTransportProvidesReadBuffer = true;
  static constexpr uint32_t kNumIovecs = 1;

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
               TransportWaitFailureHandler failure_handler);

  zx_status_t Begin() override;

  CancellationResult Cancel() override;

 private:
  void HandleChannelRead(fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         zx_status_t status);

  fidl_handle_t handle_;
  async_dispatcher_t* dispatcher_;
  TransportWaitSuccessHandler success_handler_;
  TransportWaitFailureHandler failure_handler_;
  fdf::ChannelRead channel_read_;
};

// A view into an object providing storage for messages read from a driver channel.
struct DriverMessageStorageView : public MessageStorageViewBase {
  fdf::Arena* arena;
};

inline fdf::Arena TakeDriverArenaFromStorage(MessageStorageViewBase* storage_view) {
  return std::move(*static_cast<DriverMessageStorageView*>(storage_view)->arena);
}

// Base class with common functionality for bytes and handles storage classes
// backing messages read from a driver channel.
template <typename Derived>
struct DriverMessageStorageBase {
  // |arena| backs both bytes and handles.
  fdf::Arena arena{nullptr};

  DriverMessageStorageView view() {
    return DriverMessageStorageView{
        .arena = &arena,
    };
  }
};

}  // namespace internal
}  // namespace fidl

namespace fdf {
template <typename Protocol>
class ClientEnd final
    : public fidl::internal::ClientEndBase<Protocol, fidl::internal::DriverTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::DriverTransport>);
  using ClientEndBase = fidl::internal::ClientEndBase<Protocol, fidl::internal::DriverTransport>;

 public:
  using fidl::internal::ClientEndBase<Protocol, fidl::internal::DriverTransport>::ClientEndBase;

  const fdf::Channel& channel() const { return ClientEndBase::handle_; }
  fdf::Channel& channel() { return ClientEndBase::handle_; }

  // Transfers ownership of the underlying channel to the caller.
  fdf::Channel TakeChannel() { return ClientEndBase::TakeHandle(); }
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
  using ServerEndBase = fidl::internal::ServerEndBase<Protocol, fidl::internal::DriverTransport>;

 public:
  using ServerEndBase::ServerEndBase;

  const fdf::Channel& channel() const { return ServerEndBase::handle_; }
  fdf::Channel& channel() { return ServerEndBase::handle_; }

  // Transfers ownership of the underlying channel to the caller.
  fdf::Channel TakeChannel() { return ServerEndBase::TakeHandle(); }
};

template <typename Protocol>
class UnownedServerEnd final
    : public fidl::internal::UnownedServerEndBase<Protocol, fidl::internal::DriverTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::DriverTransport>);

 public:
  using fidl::internal::UnownedServerEndBase<Protocol,
                                             fidl::internal::DriverTransport>::UnownedServerEndBase;
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
zx::result<fdf::Endpoints<Protocol>> CreateEndpoints() {
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
// endpoint, and return the corresponding server endpoint in a |zx::result|:
//
//     // |client_end_| is an instance variable.
//     auto server_end = fdf::CreateEndpoints(&client_end_);
//     if (server_end.is_ok()) { ... }
template <typename Protocol>
zx::result<fdf::ServerEnd<Protocol>> CreateEndpoints(fdf::ClientEnd<Protocol>* out_client) {
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
// endpoint, and return the corresponding client endpoint in a |zx::result|:
//
//     // |server_end_| is an instance variable.
//     auto client_end = fdf::CreateEndpoints(&server_end_);
//     if (client_end.is_ok()) { ... }
template <typename Protocol>
zx::result<fdf::ClientEnd<Protocol>> CreateEndpoints(fdf::ServerEnd<Protocol>* out_server) {
  auto endpoints = CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_server = fdf::ServerEnd<Protocol>(std::move(endpoints->server));
  return zx::ok(fdf::ClientEnd<Protocol>(std::move(endpoints->client)));
}

}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_
