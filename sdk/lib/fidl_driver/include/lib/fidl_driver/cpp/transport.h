// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/server_end.h>

namespace fidl {
namespace internal {

struct DriverHandleMetadata {};

struct DriverTransport {
  using OwnedType = fdf::Channel;
  using UnownedType = fdf::UnownedChannel;
  using HandleMetadata = DriverHandleMetadata;
  using IncomingTransportContextType = fdf_arena_t;
  using OutgoingTransportContextType = fdf_arena_t;

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
               TransportWaitFailureHandler failure_handler)
      : state_(std::make_shared<State>()) {
    state_->handle = handle;
    state_->dispatcher = dispatcher;
    state_->success_handler = std::move(success_handler);
    state_->failure_handler = std::move(failure_handler);
  }

  zx_status_t Begin() override;

  zx_status_t Cancel() override { ZX_PANIC("cancel not implemented"); }

 private:
  struct State {
    fidl_handle_t handle;
    async_dispatcher_t* dispatcher;
    TransportWaitSuccessHandler success_handler;
    TransportWaitFailureHandler failure_handler;
    std::optional<fdf::ChannelRead> channel_read;
  };
  std::shared_ptr<State> state_;
};

template <typename Protocol>
class ServerEndImpl<Protocol, internal::DriverTransport>
    : public internal::ServerEndBase<Protocol, internal::DriverTransport> {
  using ServerEndBase = internal::ServerEndBase<Protocol, internal::DriverTransport>;

 public:
  using ServerEndBase::ServerEndBase;
};

template <typename Protocol>
class ClientEndImpl<Protocol, internal::DriverTransport>
    : public internal::ClientEndBase<Protocol, internal::DriverTransport> {
  using ClientEndBase = internal::ClientEndBase<Protocol, internal::DriverTransport>;

 public:
  using ClientEndBase::ClientEndBase;
};

template <typename Protocol>
class UnownedClientEndImpl<Protocol, internal::DriverTransport>
    : public internal::UnownedClientEndBase<Protocol, internal::DriverTransport> {
  using UnownedClientEndBase = internal::UnownedClientEndBase<Protocol, internal::DriverTransport>;

 public:
  using UnownedClientEndBase::UnownedClientEndBase;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_TRANSPORT_H_
