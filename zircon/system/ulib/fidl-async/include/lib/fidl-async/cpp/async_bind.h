// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ASYNC_BIND_H_
#define LIB_FIDL_ASYNC_CPP_ASYNC_BIND_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace fidl {

template <typename Interface>
using OnChannelCloseFn = fit::callback<void(Interface*)>;

// Forward declarations.
class BindingRef;
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl);
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl,
                                               OnChannelCloseFn<Interface> on_channel_closing_fn,
                                               OnChannelCloseFn<Interface> on_channel_closed_fn);
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               std::unique_ptr<Interface> impl);
namespace internal {

class AsyncBinding;
using TypeErasedDispatchFn = bool (*)(void*, fidl_msg_t*, ::fidl::Transaction*);
using TypeErasedOnChannelCloseFn = fit::callback<void(void*)>;
fit::result<BindingRef, zx_status_t> AsyncTypeErasedBind(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedDispatchFn dispatch_fn, TypeErasedOnChannelCloseFn on_channel_closing_fn,
    TypeErasedOnChannelCloseFn on_channel_closed_fn);

}  // namespace internal

// This class abstracts a reference to a binding as described in |AsyncBind| functions below.
class BindingRef {
 public:
  // Same as AsyncBind(async_dispatcher_t*, zx::channel, Interface*) below.
  template <typename Interface>
  static fit::result<BindingRef, zx_status_t> CreateAsyncBinding(async_dispatcher_t* dispatcher,
                                                                 zx::channel channel,
                                                                 Interface* impl) {
    return AsyncBind(dispatcher, std::move(channel), impl);
  }
  // Same as AsyncBind(async_dispatcher_t*, zx::channel, Interface*,
  // OnChannelCloseFn<Interface>, OnChannelCloseFn<Interface>) below.
  template <typename Interface>
  static fit::result<BindingRef, zx_status_t> CreateAsyncBinding(
      async_dispatcher_t* dispatcher, zx::channel channel, Interface* impl,
      OnChannelCloseFn<Interface> on_channel_closing_fn,
      OnChannelCloseFn<Interface> on_channel_closed_fn) {
    return AsyncBind(dispatcher, std::move(channel), impl, std::move(on_channel_closing_fn),
                     std::move(on_channel_closed_fn));
  }
  // Same as AsyncBind(async_dispatcher_t*, zx::channel, std::unique_ptr<Interface>) below.
  template <typename Interface>
  static fit::result<BindingRef, zx_status_t> CreateAsyncBinding(async_dispatcher_t* dispatcher,
                                                                 zx::channel channel,
                                                                 std::unique_ptr<Interface> impl) {
    return AsyncBind(dispatcher, std::move(channel), std::move(impl));
  }

  // Move only.
  BindingRef(BindingRef&&) = default;
  BindingRef& operator=(BindingRef&&) = default;
  BindingRef(const BindingRef&) = delete;
  BindingRef& operator=(const BindingRef&) = delete;

  // Forces unbind without waiting for transactions to get destroyed. Once it returns the unbind
  // is completed and the binding is destroyed. Must be called from the dispatcher thread.
  // Once the binding is destroyed, the channel is closed, we stop waiting on messages in the
  // dispatcher, and any in-flight transactions |Reply|/|Close| will have no effect.
  void Unbind();

 private:
  friend fit::result<BindingRef, zx_status_t> internal::AsyncTypeErasedBind(
      async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
      internal::TypeErasedDispatchFn dispatch_fn,
      internal::TypeErasedOnChannelCloseFn on_channel_closing_fn,
      internal::TypeErasedOnChannelCloseFn on_channel_closed_fn);

  explicit BindingRef(std::shared_ptr<internal::AsyncBinding> internal_binding)
      : binding_(std::move(internal_binding)) {}

  std::shared_ptr<internal::AsyncBinding> binding_;
};

// Binds an implementation of a low-level C++ server interface to |channel| using a
// single-threaded |dispatcher|.
// This implementation allows for multiple in-flight asynchronous transactions.
//
// This function adds an |async::WaitMethod| to the given single-threaded |dispatcher| that waits
// asynchronously for new messages to arrive on |channel|. When a message arrives, the dispatch
// function corresponding to the interface is called on the |dispatcher| thread.
//
// Typically, the dispatch function is generated by the low-level C++ backend
// for FIDL interfaces. These dispatch functions decode the |fidl_msg_t| and
// call into the implementation of the FIDL interface, via its C++ vtable.
//
// Creation:
// - Upon success |AsyncBind| creates a binding that owns |channel|. In this case, the binding is
//   initially kept alive even if the returned fit::result with a |BindingRef| is ignored.
// - Upon any error creating the binding, |AsyncBind| returns a fit::error and |channel| is closed.
//
// Destruction:
// - If the returned |BindingRef| is ignored or dropped some time during the server operation,
//   then if some error occurs (see below) the binding will be automatically destroyed.
// - If the returned |BindingRef| is kept, even if some error occurs (see below) the binding will
//   be kept alive until the returned |BindingRef| is destroyed.
// - When the binding is destroyed, it won't receive new messages in the channel, in-flight
//   transactions |Reply|/|Close| calls will have no effect, an epitaph is sent (unless the error
//   was a PEER_CLOSED) and the channel is closed.
// - Destruction may be slightly delayed due to binding usage by transactions.
//
// Unbind:
// - The returned |BindingRef| can be used to explicitly |Unbind| the binding.
// - After |Unbind| returns the binding is destroyed.
//
// Error conditions:
// - When an error occurs in the server implementation as part of handling a message, it may call
//   |Close| on the completer to indicate the error condition.
// - If the client end of the channel gets closed (PEER_CLOSED).
// - If an error occurs in the binding itself, e.g. a channel write fails.
//
// TODO(38455): Specify reason in close related callbacks (suggestion by yifeit@).
// TODO(38456): Add support for multithreaded dispatchers.
// TODO(40787): on_closed callback removal.
//
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl) {
  return internal::AsyncTypeErasedBind(dispatcher, std::move(channel), impl,
                                       &Interface::_Outer::TypeErasedDispatch, nullptr, nullptr);
}

// As above, but will invoke |on_channel_closing_fn| on |impl| when either end of the |channel|
// is being closed, this notification allows in-flight transactions to be cancelled.
// |Unbind| calls from a |BindingRef| won't invoke |on_channel_closing_fn|.
// |on_channel_closed_fn| is called before the channel is closed as part of the binding destruction.
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl,
                                               OnChannelCloseFn<Interface> on_channel_closing_fn,
                                               OnChannelCloseFn<Interface> on_channel_closed_fn) {
  return internal::AsyncTypeErasedBind(
      dispatcher, std::move(channel), impl, &Interface::_Outer::TypeErasedDispatch,
      [fn = std::move(on_channel_closing_fn)](void* impl) mutable {
        fn(static_cast<Interface*>(impl));
      },
      [fn = std::move(on_channel_closed_fn)](void* impl) mutable {
        fn(static_cast<Interface*>(impl));
      });
}

// As above, but will destroy |impl| as the binding is destroyed and the |channel| closed.
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               std::unique_ptr<Interface> impl) {
  OnChannelCloseFn<Interface> on_closing = [](Interface* impl) {};
  OnChannelCloseFn<Interface> on_closed = [](Interface* impl) { delete impl; };
  return AsyncBind(dispatcher, std::move(channel), impl.release(), std::move(on_closing),
                   std::move(on_closed));
}

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ASYNC_BIND_H_
