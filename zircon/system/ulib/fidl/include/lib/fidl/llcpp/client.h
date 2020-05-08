// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_H_
#define LIB_FIDL_LLCPP_CLIENT_H_

#include <lib/fidl/llcpp/client_base.h>

namespace fidl {

// Invoked from a dispatcher thread after the channel is unbound.
using OnClientUnboundFn = fit::callback<void(UnboundReason, zx_status_t, zx::channel)>;

template <typename Protocol>
class Client final {
 public:
  // Create uninitialized Client.
  Client() = default;

  // Create initialized Client which manages the binding of the client end of a channel to a
  // dispatcher.
  Client(zx::channel client_end, async_dispatcher_t* dispatcher,
            typename Protocol::AsyncEventHandlers handlers = {}) {
    auto status = Bind(std::move(client_end), dispatcher, std::move(handlers));
    ZX_ASSERT_MSG(status == ZX_OK, "%s: Failed Bind() with status %d.", __func__, status);
  }

  Client(zx::channel client_end, async_dispatcher_t* dispatcher, OnClientUnboundFn on_unbound,
            typename Protocol::AsyncEventHandlers handlers = {}) {
    auto status = Bind(std::move(client_end), dispatcher, std::move(on_unbound),
                       std::move(handlers));
    ZX_ASSERT_MSG(status == ZX_OK, "%s: Failed Bind() with status %d.", __func__, status);
  }

  ~Client() { Unbind(); }

  // Move-only.
  Client(const Client& other) = delete;
  Client& operator=(const Client& other) = delete;
  Client(Client&& other) = default;
  Client& operator=(Client&& other) = default;

  // Bind the channel to the dispatcher. May only be called on an uninitialized Client() and
  // at most once.
  zx_status_t Bind(zx::channel client_end, async_dispatcher_t* dispatcher,
                   OnClientUnboundFn on_unbound,
                   typename Protocol::AsyncEventHandlers handlers = {}) {
    ZX_ASSERT(!client_);
    client_ = std::unique_ptr<typename Protocol::ClientImpl>(new typename Protocol::ClientImpl(
        std::move(client_end), dispatcher,
        [fn = std::move(on_unbound)](void*, UnboundReason reason, zx_status_t status,
                                     zx::channel channel) mutable {
          fn(reason, status, std::move(channel));
        }, std::move(handlers)));
    return client_->Bind();
  }

  zx_status_t Bind(zx::channel client_end, async_dispatcher_t* dispatcher,
                   typename Protocol::AsyncEventHandlers handlers = {}) {
    ZX_ASSERT(!client_);
    client_ = std::unique_ptr<typename Protocol::ClientImpl>(
        new typename Protocol::ClientImpl(std::move(client_end), dispatcher, nullptr,
                                          std::move(handlers)));
    return client_->Bind();
  }

  // Unbind the channel from the dispatcher. May be called from any thread. If provided, the
  // OnClientUnboundFn is invoked asynchronously on a dispatcher thread.
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // OnClinetUnboundFn from a dispatcher thread, as that will likely deadlock.
  void Unbind() { if (client_) client_->internal::ClientBase::Unbind(); }

  // Return the Client interface for making outgoing FIDL calls. If the client has been unbound,
  // calls on the interface return error with status ZX_ERR_CANCELED.
  typename Protocol::ClientImpl* get() const { return client_.get(); }
  typename Protocol::ClientImpl* operator->() const { return get(); }
  typename Protocol::ClientImpl& operator*() const { return *get(); }

 private:
  std::unique_ptr<typename Protocol::ClientImpl> client_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_H_
