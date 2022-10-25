// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_TRANSPORT_TRANSPORT_SOCKET_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_TRANSPORT_TRANSPORT_SOCKET_H_

#include <lib/async/dispatcher.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zx/socket.h>
#include <zircon/syscalls.h>

namespace fidl {
namespace socket {
template <typename Protocol>
class ClientEnd;
template <typename Protocol>
class UnownedClientEnd;
template <typename Protocol>
class ServerEnd;
template <typename Protocol>
class ServerBindingRef;
}  // namespace socket

namespace internal {

// A view into an object providing storage for messages read from a Zircon socket.
struct SocketMessageStorageView : public MessageStorageViewBase {
  fidl::BufferSpan bytes;
};

struct SocketHandleMetadata {};

struct SocketTransport {
  using OwnedType = zx::socket;
  using UnownedType = zx::unowned_socket;
  using HandleMetadata = SocketHandleMetadata;
  using OutgoingTransportContextType = struct {};
  template <typename Protocol>
  using ClientEnd = fidl::socket::ClientEnd<Protocol>;
  template <typename Protocol>
  using UnownedClientEnd = fidl::socket::UnownedClientEnd<Protocol>;
  template <typename Protocol>
  using ServerEnd = fidl::socket::ServerEnd<Protocol>;
  template <typename Protocol>
  using ServerBindingRef = fidl::socket::ServerBindingRef<Protocol>;
  using MessageStorageView = SocketMessageStorageView;

  static constexpr bool kTransportProvidesReadBuffer = false;
  static constexpr uint32_t kNumIovecs = 1;

  static const TransportVTable VTable;
  static const CodingConfig EncodingConfiguration;
};

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
  CancellationResult Cancel() override {
    zx_status_t status = async_cancel_wait(dispatcher_, static_cast<async_wait_t*>(this));
    switch (status) {
      case ZX_OK:
        return CancellationResult::kOk;
      case ZX_ERR_NOT_FOUND:
        return CancellationResult::kNotFound;
      case ZX_ERR_NOT_SUPPORTED:
        return CancellationResult::kNotSupported;
      default:
        ZX_PANIC("Unexpected status %d", status);
    }
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

namespace socket {
template <typename Protocol>
class ClientEnd final : public internal::ClientEndBase<Protocol, internal::SocketTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, internal::SocketTransport>);

 public:
  using internal::ClientEndBase<Protocol, internal::SocketTransport>::ClientEndImpl;
};

template <typename Protocol>
class UnownedClientEnd final
    : public internal::UnownedClientEndBase<Protocol, internal::SocketTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, internal::SocketTransport>);

 public:
  using internal::UnownedClientEndBase<Protocol, internal::SocketTransport>::UnownedClientEndBase;
};

template <typename Protocol>
class ServerEnd final : public internal::ServerEndBase<Protocol, internal::SocketTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::SocketTransport>);
  using ServerEndBase = internal::ServerEndBase<Protocol, internal::SocketTransport>;

 public:
  using ServerEndBase::ServerEndBase;
};

template <typename Protocol>
class ServerBindingRef : public fidl::internal::ServerBindingRefBase {
 public:
  // Triggers an asynchronous unbind operation. If specified, |on_unbound| will be invoked on a
  // dispatcher thread, passing in the channel and the unbind reason. On return, the dispatcher
  // will no longer have any wait associated with the channel (though handling of any already
  // in-flight transactions will continue).
  //
  // This may be called from any thread.
  //
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // OnUnboundFn from a dispatcher thread, as that will likely deadlock.
  using ServerBindingRefBase::Unbind;

 private:
  // This is so that only |BindServerTypeErased| will be able to construct a
  // new instance of |ServerBindingRef|.
  friend internal::ServerBindingRefType<Protocol> internal::BindServerTypeErased<Protocol>(
      async_dispatcher_t* dispatcher, fidl::internal::ServerEndType<Protocol> server_end,
      internal::IncomingMessageDispatcher* interface, internal::ThreadingPolicy threading_policy,
      internal::AnyOnUnboundFn on_unbound);

  explicit ServerBindingRef(std::weak_ptr<internal::AsyncServerBinding> internal_binding)
      : ServerBindingRefBase(std::move(internal_binding)) {}
};

template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    async_dispatcher_t* dispatcher, ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    ServerImpl* impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::SocketTransport>);
  return fidl::internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl,
      fidl::internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    async_dispatcher_t* dispatcher, ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    std::unique_ptr<ServerImpl>&& impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::SocketTransport>);
  ServerImpl* impl_raw = impl.get();
  return fidl::internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl_raw,
      fidl::internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    async_dispatcher_t* dispatcher, ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    std::shared_ptr<ServerImpl> impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::SocketTransport>);
  ServerImpl* impl_raw = impl.get();
  return fidl::internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl_raw,
      fidl::internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}
}  // namespace socket

}  // namespace fidl

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_TRANSPORT_TRANSPORT_SOCKET_H_
