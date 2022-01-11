// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_H_
#define LIB_FIDL_LLCPP_SERVER_H_

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/fidl/llcpp/internal/server_details.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

namespace fidl {

template <typename Protocol, typename Transport>
class ServerBindingRefImpl {
 public:
  ~ServerBindingRefImpl() = default;

  ServerBindingRefImpl(ServerBindingRefImpl&&) noexcept = default;
  ServerBindingRefImpl& operator=(ServerBindingRefImpl&&) noexcept = default;

  ServerBindingRefImpl(const ServerBindingRefImpl&) = default;
  ServerBindingRefImpl& operator=(const ServerBindingRefImpl&) = default;

  // Triggers an asynchronous unbind operation. If specified, |on_unbound| will be invoked on a
  // dispatcher thread, passing in the channel and the unbind reason. On return, the dispatcher
  // will no longer have any wait associated with the channel (though handling of any already
  // in-flight transactions will continue).
  //
  // This may be called from any thread.
  //
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // OnUnboundFn from a dispatcher thread, as that will likely deadlock.
  void Unbind() {}

 private:
  // This is so that only |BindServerTypeErased| will be able to construct a
  // new instance of |ServerBindingRef|.
  friend internal::ServerBindingRefType<Protocol> internal::BindServerTypeErased<Protocol>(
      async_dispatcher_t* dispatcher, fidl::internal::ServerEndType<Protocol> server_end,
      internal::IncomingMessageDispatcher* interface, internal::AnyOnUnboundFn on_unbound);

  explicit ServerBindingRefImpl(std::weak_ptr<internal::AsyncServerBinding>) {}
};

template <typename Protocol>
class ServerBindingRefImpl<Protocol, fidl::internal::ChannelTransport> {
 public:
  ~ServerBindingRefImpl() = default;

  ServerBindingRefImpl(ServerBindingRefImpl&&) noexcept = default;
  ServerBindingRefImpl& operator=(ServerBindingRefImpl&&) noexcept = default;

  ServerBindingRefImpl(const ServerBindingRefImpl&) = default;
  ServerBindingRefImpl& operator=(const ServerBindingRefImpl&) = default;

  // Triggers an asynchronous unbind operation. If specified, |on_unbound| will be invoked on a
  // dispatcher thread, passing in the channel and the unbind reason. On return, the dispatcher
  // will no longer have any wait associated with the channel (though handling of any already
  // in-flight transactions will continue).
  //
  // This may be called from any thread.
  //
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // OnUnboundFn from a dispatcher thread, as that will likely deadlock.
  void Unbind() {
    if (auto binding = event_sender_.inner_.binding().lock())
      binding->StartTeardown(std::move(binding));
  }

  // Triggers an asynchronous unbind operation. Eventually, the epitaph will be sent over the
  // channel which will be subsequently closed. If specified, |on_unbound| will be invoked giving
  // the unbind reason as an argument.
  //
  // This may be called from any thread.
  void Close(zx_status_t epitaph) {
    if (auto binding = event_sender_.inner_.binding().lock())
      binding->Close(std::move(binding), epitaph);
  }

  // Return the interface for sending FIDL events. If the server has been unbound, calls on the
  // interface return error with status ZX_ERR_CANCELED.
  //
  // Persisting this pointer to a local variable is discouraged, since that
  // results in unsafe borrows. Always prefer making calls directly via the
  // |fidl::ServerBindingRef| reference-counting type.
  const fidl::internal::WireWeakEventSender<Protocol>* operator->() const { return &event_sender_; }
  const fidl::internal::WireWeakEventSender<Protocol>& operator*() const { return event_sender_; }

 private:
  // This is so that only |BindServerTypeErased| will be able to construct a
  // new instance of |ServerBindingRef|.
  friend internal::ServerBindingRefType<Protocol> internal::BindServerTypeErased<Protocol>(
      async_dispatcher_t* dispatcher, fidl::internal::ServerEndType<Protocol> server_end,
      internal::IncomingMessageDispatcher* interface, internal::AnyOnUnboundFn on_unbound);

  explicit ServerBindingRefImpl(std::weak_ptr<internal::AsyncServerBinding> internal_binding)
      : event_sender_(std::move(internal_binding)) {}

  fidl::internal::WireWeakEventSender<Protocol> event_sender_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_H_
