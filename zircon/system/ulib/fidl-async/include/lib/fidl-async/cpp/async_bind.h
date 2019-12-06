// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ASYNC_BIND_H_
#define LIB_FIDL_ASYNC_CPP_ASYNC_BIND_H_

#include <lib/fidl/llcpp/transaction.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace fidl {

enum class UnboundReason {
  // The user invoked Unbind() or Close().
  kUnbind,
  // The channel peer was closed.
  kPeerClosed,
  // An unexpected channel read/write error or dispatcher error occurred.
  kInternalError,
};

template <typename Interface>
using OnUnboundFn = fit::callback<void(Interface*, UnboundReason, zx::channel)>;

// Forward declarations.
class BindingRef;
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl);
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl,
                                               OnUnboundFn<Interface> on_unbound);
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               std::unique_ptr<Interface> impl);

namespace internal {

class AsyncBinding;
using TypeErasedDispatchFn = bool (*)(void*, fidl_msg_t*, ::fidl::Transaction*);
using TypeErasedOnUnboundFn = fit::callback<void(void*, UnboundReason, zx::channel)>;
fit::result<BindingRef, zx_status_t> AsyncTypeErasedBind(
    async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
    TypeErasedDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound);

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
  // Same as AsyncBind(async_dispatcher_t*, zx::channel, Interface*, OnUnboundFn<Interface>)
  // below.
  template <typename Interface>
  static fit::result<BindingRef, zx_status_t> CreateAsyncBinding(
      async_dispatcher_t* dispatcher, zx::channel channel, Interface* impl,
      OnUnboundFn<Interface> on_unbound) {
    return AsyncBind(dispatcher, std::move(channel), impl, std::move(on_unbound));
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

  // Triggers an asynchronous unbind operation. If specified, the OnUnboundFn<> will be invoked on
  // a dispatcher thread, passing in the channel and the unbind reason. In an error case, the
  // channel will have been closed.
  //
  // This may be called from any thread.
  //
  // NOTE: For a single-threaded dispatcher, if Unbind() is invoked from a dispatcher thread and
  // outside the scope of the message handler for the same binding, the channel will have been fully
  // unbound on return, i.e. no other threads will be able to access it. Note that the OnUnboundFn<>
  // will still be executed asynchronously if specified.
  void Unbind();

  // TODO(madhaviyengar): Re-introduce synchronous Unbind() which returns a zx::channel.

  // Triggers an asynchronous unbind operation. Eventually, the epitaph will be sent over the
  // channel which will subsequently be closed. If specified, the OnUnboundFn<> will be invoked
  // giving the unbind reason as an argument.
  //
  // This may be called from any thread.
  void Close(zx_status_t epitaph);

 private:
  friend fit::result<BindingRef, zx_status_t> internal::AsyncTypeErasedBind(
      async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
      internal::TypeErasedDispatchFn dispatch_fn, internal::TypeErasedOnUnboundFn on_unbound);

  explicit BindingRef(std::shared_ptr<internal::AsyncBinding> internal_binding)
      : binding_(std::move(internal_binding)) {}

  std::shared_ptr<internal::AsyncBinding> binding_;
};

// Binds an implementation of a low-level C++ server interface to |channel| using a potentially
// multi-threaded |dispatcher|. This implementation allows for multiple in-flight synchronously or
// asynchronously handled transactions.
//
// This function adds an asynchronous wait to the given |dispatcher| for new messages to arrive on
// |channel|. When a message arrives, the dispatch function corresponding to the interface is called
// on one of the |dispatcher| threads.
//
// Typically, the dispatch function is generated by the low-level C++ backend for FIDL interfaces.
// These dispatch functions decode the |fidl_msg_t| and call into the implementation of the FIDL
// interface, via its C++ vtable.
//
// Creation:
// - Upon success |AsyncBind| creates a binding that owns |channel|. In this case, the binding is
//   initially kept alive even if the returned fit::result with a |BindingRef| is ignored.
// - Upon any error creating the binding, |AsyncBind| returns a fit::error and |channel| is closed.
//
// Destruction:
// - If the returned |BindingRef| is ignored or dropped some time during the server operation,
//   then if some error occurs (see below) the binding will be automatically destroyed.
// - If the returned |BindingRef| is kept but an error occurs (see below), the binding will be
//   destroyed, though calls may still be made on the |BindingRef|.
// - On an error, |channel| is unbound from the dispatcher, i.e. no dispatcher threads will interact
//   with it. The server end is closed if necessary, and calls on inflight |Transaction|s will have
//   no effect. If specified, |on_unbound| is then executed on a |dispatcher| thread allowing the
//   user to process the error.
//
// Unbind:
// - The |BindingRef| can be used to explicitly |Unbind| the binding and retrieve the |channel|
//   endpoint.
// - |Unbind| is non-blocking with respect to user code paths, i.e. if it blocks, it does so on
//   deterministic internal code paths. As such, the user may safely synchronize around an |Unbind|
//   call.
// - In order to reclaim the |channel|, the user must specify an OnUnboundFn<> hook. This will be
//   invoked after the |channel| has been unbound from the |dispatcher|. If no error occurs, the
//   channel will be given as an argument to the hook.
//
// Close:
// - |Close| is similar to |Unbind| except that it causes an epitaph message to be sent on the
//   |channel| which is subsequently closed.
// - If specified, the OnUnboundFn<> hook will execute after the epitaph has been sent and will be
//   given an invalid handle for the |channel|.
//
// Error conditions:
// - When an error occurs in the server implementation as part of handling a message, it may call
//   |Close| on the completer to indicate the error condition.
// - If the client end of the channel gets closed (PEER_CLOSED).
// - If an error occurs in the binding itself, e.g. a channel write fails.
//
// Ordering:
// - By default, the message dispatch function for a binding will only ever be invoked by a single
//   |dispatcher| thread at a time.
// - To enable more concurrency, the user may invoke |ResumeDispatch| on the
//   |fidl::Completer<T>::Sync| from the dispatch function. This will resume the async wait on the
//   |dispatcher| before the dispatch function returns, allowing other |dispatcher| threads to
//   handle messages for the same binding.
// NOTE: If a particular user does not care about ordering, they may invoke |ResumeDispatch|
// immediately in the message handler. However, this functionality could instead be provided as a
// default configuration. If you have such a usecase, please contact madhaviyengar@ or yifei@.
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl) {
  return internal::AsyncTypeErasedBind(
      dispatcher, std::move(channel), impl, &Interface::_Outer::TypeErasedDispatch, nullptr);
}

// As above, but will invoke |on_unbound| on |impl| when the channel is being unbound, either due to
// error or an explicit |Close| or |Unbind|.
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               Interface* impl, OnUnboundFn<Interface> on_unbound) {
  return internal::AsyncTypeErasedBind(
      dispatcher, std::move(channel), impl, &Interface::_Outer::TypeErasedDispatch,
      [fn = std::move(on_unbound)](void* impl, UnboundReason reason, zx::channel channel) mutable {
        fn(static_cast<Interface*>(impl), reason, std::move(channel));
      });
}

// As above, but will destroy |impl| whenever the binding is destroyed.
template <typename Interface>
fit::result<BindingRef, zx_status_t> AsyncBind(async_dispatcher_t* dispatcher, zx::channel channel,
                                               std::unique_ptr<Interface> impl) {
  Interface* impl_raw = impl.get();
  return internal::AsyncTypeErasedBind(
      dispatcher, std::move(channel), impl_raw, &Interface::_Outer::TypeErasedDispatch,
      [intf = std::move(impl)](void*, UnboundReason, zx::channel){});
}

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ASYNC_BIND_H_
