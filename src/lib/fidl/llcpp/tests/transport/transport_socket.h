// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TRANSPORT_TRANSPORT_SOCKET_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TRANSPORT_TRANSPORT_SOCKET_H_

#include <lib/async/dispatcher.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/zx/socket.h>
#include <zircon/syscalls.h>

namespace fidl {
namespace internal {

struct SocketHandleMetadata {};

struct SocketTransport {
  using OwnedType = zx::socket;
  using UnownedType = zx::unowned_socket;
  using HandleMetadata = SocketHandleMetadata;

  static const TransportVTable VTable;
  static const CodingConfig EncodingConfiguration;
};

AnyTransport MakeAnyTransport(zx::socket socket);
AnyUnownedTransport MakeAnyUnownedTransport(const zx::socket& socket);
AnyUnownedTransport MakeAnyUnownedTransport(const zx::unowned_socket& socket);

template <>
struct AssociatedTransportImpl<zx::socket> {
  using type = SocketTransport;
};
template <>
struct AssociatedTransportImpl<zx::unowned_socket> {
  using type = SocketTransport;
};

template <>
struct AssociatedTransportImpl<SocketHandleMetadata> {
  using type = SocketTransport;
};

class SocketWaiter : private async_wait_t, public TransportWaiter {
 public:
  SocketWaiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
               TransportWaitSuccessHandler success_handler,
               TransportWaitFailureHandler failure_handler)
      : async_wait_t({{ASYNC_STATE_INIT},
                      &SocketWaiter::OnWaitFinished,
                      handle,
                      ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READABLE,
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
    static_cast<SocketWaiter*>(wait)->HandleWaitFinished(dispatcher, status, signal);
  }

  void HandleWaitFinished(async_dispatcher_t* dispatcher, zx_status_t status,
                          const zx_packet_signal_t* signal);

  async_dispatcher_t* dispatcher_;
  TransportWaitSuccessHandler success_handler_;
  TransportWaitFailureHandler failure_handler_;
};

}  // namespace internal

template <typename Protocol>
class ServerEnd<Protocol, internal::SocketTransport>
    : public internal::ServerEndBase<Protocol, internal::SocketTransport> {
  using ServerEndBase = internal::ServerEndBase<Protocol, internal::SocketTransport>;

 public:
  using ServerEndBase::ServerEndBase;
};

template <typename Protocol>
class ClientEnd<Protocol, internal::SocketTransport> final
    : public internal::ClientEndBase<Protocol, internal::SocketTransport> {
  using ClientEndBase = internal::ClientEndBase<Protocol, internal::SocketTransport>;

 public:
  using ClientEndBase::ClientEndBase;
};

template <typename Protocol>
class UnownedClientEnd<Protocol, internal::SocketTransport> final
    : public internal::UnownedClientEndBase<Protocol, internal::SocketTransport> {
  using UnownedClientEndBase = internal::UnownedClientEndBase<Protocol, internal::SocketTransport>;

 public:
  using UnownedClientEndBase::UnownedClientEndBase;
};

}  // namespace fidl

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_TRANSPORT_TRANSPORT_SOCKET_H_
