// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_H_

#include <lib/fidl/llcpp/internal/transport.h>

#ifdef __Fuchsia__
#include <lib/async/dispatcher.h>
#include <lib/async/wait.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>
#endif

namespace fidl::internal {

struct ChannelTransport {
#ifdef __Fuchsia__
  using OwnedType = zx::channel;
  using UnownedType = zx::unowned_channel;
#endif
  using HandleMetadata = fidl_channel_handle_metadata_t;

  static const TransportVTable VTable;
  static const CodingConfig EncodingConfiguration;
};

#ifdef __Fuchsia__
AnyTransport MakeAnyTransport(zx::channel channel);
AnyUnownedTransport MakeAnyUnownedTransport(const zx::channel& channel);
AnyUnownedTransport MakeAnyUnownedTransport(const zx::unowned_channel& channel);

template <>
struct AssociatedTransportImpl<zx::channel> {
  using type = ChannelTransport;
};
template <>
struct AssociatedTransportImpl<zx::unowned_channel> {
  using type = ChannelTransport;
};
#endif

template <>
struct AssociatedTransportImpl<fidl_channel_handle_metadata_t> {
  using type = ChannelTransport;
};

#ifdef __Fuchsia__
static_assert(sizeof(fidl_handle_t) == sizeof(zx_handle_t));

class ChannelWaiter : private async_wait_t, public TransportWaiter {
 public:
  ChannelWaiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                TransportWaitSuccessHandler success_handler,
                TransportWaitFailureHandler failure_handler)
      : async_wait_t({{ASYNC_STATE_INIT},
                      &ChannelWaiter::OnWaitFinished,
                      handle,
                      ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                      0}),
        dispatcher_(dispatcher),
        success_handler_(std::move(success_handler)),
        failure_handler_(std::move(failure_handler)) {}
  zx_status_t Begin() override {
    return async_begin_wait(dispatcher_, static_cast<async_wait_t*>(this));
  }
  zx_status_t Cancel() override {
    return async_cancel_wait(dispatcher_, static_cast<async_wait_t*>(this));
  }

 private:
  static void OnWaitFinished(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                             const zx_packet_signal_t* signal) {
    static_cast<ChannelWaiter*>(wait)->HandleWaitFinished(dispatcher, status, signal);
  }

  void HandleWaitFinished(async_dispatcher_t* dispatcher, zx_status_t status,
                          const zx_packet_signal_t* signal);

  async_dispatcher_t* dispatcher_;
  TransportWaitSuccessHandler success_handler_;
  TransportWaitFailureHandler failure_handler_;
};
#endif

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_H_
