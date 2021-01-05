// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_H_
#define LIB_FIDL_LLCPP_CLIENT_H_

#include <lib/fidl/llcpp/client_base.h>

namespace fidl {

// This class wraps the LLCPP thread-safe client. It provides methods for binding a channel's
// client end to a dispatcher, unbinding the channel, and recovering the channel. Generated FIDL
// APIs are accessed by 'dereferencing' the Client.
//
// This class itself is NOT thread-safe. The user is responsible for ensuring that Bind() is
// serialized with respect to other calls on Client APIs. Also, Bind() must have been called before
// any calls are made to other APIs.
// NOTE: Using the constructor overloads automatically satisfies the latter condition.
template <typename Protocol>
class Client final {
 public:
  // Create uninitialized Client.
  Client() = default;

  // Create initialized Client which manages the binding of the client end of a channel to a
  // dispatcher.
  Client(zx::channel client_end, async_dispatcher_t* dispatcher,
         std::shared_ptr<typename Protocol::AsyncEventHandler> event_handler = nullptr) {
    auto status = Bind(std::move(client_end), dispatcher, std::move(event_handler));
    ZX_ASSERT_MSG(status == ZX_OK, "%s: Failed Bind() with status %d.", __func__, status);
  }

  // Trigger unbinding, which will cause any strong references to the ClientBase to be released.
  ~Client() {
    if (client_)
      client_->ClientBase::Unbind();
  }

  // Move-only.
  Client(const Client& other) = delete;
  Client& operator=(const Client& other) = delete;
  Client(Client&& other) = default;
  Client& operator=(Client&& other) = default;

  // Bind the channel to the dispatcher. If Client is already initialized, destroys the previous
  // binding, releasing its channel.
  zx_status_t Bind(zx::channel client_end, async_dispatcher_t* dispatcher,
                   std::shared_ptr<typename Protocol::AsyncEventHandler> event_handler = nullptr) {
    if (client_)
      client_->ClientBase::Unbind();
    client_.reset(new typename Protocol::ClientImpl(event_handler));
    return client_->Bind(std::reinterpret_pointer_cast<internal::ClientBase>(client_),
                         std::move(client_end), dispatcher,
                         [event_handler](UnbindInfo unbind_info) {
                           if (event_handler != nullptr) {
                             event_handler->Unbound(unbind_info);
                           }
                         });
  }

  // Unbind the channel from the dispatcher. May be called from any thread. If provided, the
  // |Protocol::AsyncEventHandler::Unbound| is invoked asynchronously on a dispatcher thread.
  // NOTE: Bind() must have been called before this.
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // |Protocol::AsyncEventHandler::Unbound from a dispatcher thread, as that will likely deadlock.
  void Unbind() {
    ZX_ASSERT(client_);
    client_->ClientBase::Unbind();
  }

  // Returns the underlying channel. Unbinds from the dispatcher if required.
  // NOTE: Bind() must have been called before this.
  // WARNING: This is a blocking call. It waits for completion of dispatcher unbind and of any
  // channel operations, including synchronous calls which may block indefinitely.
  zx::channel WaitForChannel() {
    ZX_ASSERT(client_);
    return client_->WaitForChannel();
  }

  // Return the Client interface for making outgoing FIDL calls. If the client has been unbound,
  // calls on the interface return error with status ZX_ERR_CANCELED.
  typename Protocol::ClientImpl* get() const {
    return static_cast<typename Protocol::ClientImpl*>(client_.get());
  }
  typename Protocol::ClientImpl* operator->() const { return get(); }
  typename Protocol::ClientImpl& operator*() const { return *get(); }

 private:
  std::shared_ptr<typename Protocol::ClientImpl> client_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_H_
