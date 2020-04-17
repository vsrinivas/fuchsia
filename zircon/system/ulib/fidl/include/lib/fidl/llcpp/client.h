// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_H_
#define LIB_FIDL_LLCPP_CLIENT_H_

#include <lib/fidl/llcpp/client_base.h>

namespace fidl {

// Invoked from a dispatcher thread after the channel is unbound.
using OnClientUnboundFn = fit::callback<void(UnboundReason, zx_status_t, zx::channel)>;

template <typename Client>
class ClientPtr final {
 public:
  // Create uninitialized ClientPtr.
  ClientPtr() = default;

  // Create initialized ClientPtr which manages the binding of the client end of a channel to a
  // dispatcher.
  ClientPtr(zx::channel client_end, async_dispatcher_t* dispatcher,
            typename Client::EventHandlers handlers = {}) {
    auto status = Bind(std::move(client_end), dispatcher, std::move(handlers));
    ZX_ASSERT_MSG(status == ZX_OK, "%s: Failed Bind() with status %d.", __func__, status);
  }

  ClientPtr(zx::channel client_end, async_dispatcher_t* dispatcher, OnClientUnboundFn on_unbound,
            typename Client::EventHandlers handlers = {}) {
    auto status = Bind(std::move(client_end), dispatcher, std::move(on_unbound),
                       std::move(handlers));
    ZX_ASSERT_MSG(status == ZX_OK, "%s: Failed Bind() with status %d.", __func__, status);
  }

  ~ClientPtr() { Unbind(); }

  // Move-only.
  ClientPtr(const ClientPtr& other) = delete;
  ClientPtr& operator=(const ClientPtr& other) = delete;
  ClientPtr(ClientPtr&& other) = default;
  ClientPtr& operator=(ClientPtr&& other) = default;

  // Bind the channel to the dispatcher. May only be called on an uninitialized ClientPtr() and
  // at most once.
  zx_status_t Bind(zx::channel client_end, async_dispatcher_t* dispatcher,
                   OnClientUnboundFn on_unbound, typename Client::EventHandlers handlers = {}) {
    ZX_ASSERT(!binding_);
    binding_ = std::make_unique<internal::ClientBase>(
        std::move(client_end), dispatcher,
        [fn = std::move(on_unbound)](void*, UnboundReason reason, zx_status_t status,
                                     zx::channel channel) mutable {
          fn(reason, status, std::move(channel));
        });
    client_ = std::make_unique<Client>(binding_.get(), std::move(handlers));
    return binding_->Bind();
  }

  zx_status_t Bind(zx::channel client_end, async_dispatcher_t* dispatcher,
                   typename Client::EventHandlers handlers = {}) {
    ZX_ASSERT(!binding_);
    binding_ = std::make_unique<internal::ClientBase>(std::move(client_end), dispatcher, nullptr);
    client_ = std::make_unique<Client>(binding_.get(), std::move(handlers));
    return binding_->Bind();
  }

  // Unbind the channel from the dispatcher. If provided, invoke the OnUnboundFn on a dispatcher
  // thread.
  void Unbind() { binding_->Unbind(); }

  // Return the Client interface for making outgoing FIDL calls. If the client has been unbound,
  // calls on the interface return error with status ZX_ERR_CANCELED.
  Client* get() const { return client_.get(); }
  Client* operator->() const { return get(); }
  Client& operator*() const { return *get(); }

 private:
  std::unique_ptr<internal::ClientBase> binding_;
  std::unique_ptr<Client> client_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_H_
