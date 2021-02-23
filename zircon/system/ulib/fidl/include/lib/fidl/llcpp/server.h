// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_H_
#define LIB_FIDL_LLCPP_SERVER_H_

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/server_end.h>

namespace fidl {

// Forward declarations.
template <typename Protocol>
class ServerBindingRef;

namespace internal {

// The interface for dispatching incoming FIDL messages. The code generator
// will provide conforming implementations for relevant FIDL protocols.
class IncomingMessageDispatcher {
 public:
  virtual ~IncomingMessageDispatcher() = default;

  // Dispatches an incoming message to one of the handlers functions in the
  // protocol. If there is no matching handler, closes all the handles in
  // |msg| and closes the channel with a |ZX_ERR_NOT_SUPPORTED| epitaph, before
  // returning false. The message should then be discarded.
  //
  // Note that the |dispatch_message| name avoids conflicts with FIDL method
  // names which would appear on the subclasses.
  //
  // Always consumes the handles in |msg|.
  virtual ::fidl::DispatchResult dispatch_message(fidl_incoming_msg_t* msg,
                                                  ::fidl::Transaction* txn) = 0;
};

template <typename Protocol>
fit::result<ServerBindingRef<Protocol>, zx_status_t> BindServerTypeErased(
    async_dispatcher_t* dispatcher, fidl::ServerEnd<Protocol> server_end,
    internal::IncomingMessageDispatcher* interface, internal::AnyOnUnboundFn on_unbound);

// Defines an incoming method entry. Used by a server to dispatch an incoming message.
struct MethodEntry {
  // The ordinal of the method handled by the entry.
  uint64_t ordinal;
  // The coding table of the method (used to decode the message).
  const fidl_type_t* type;
  // The function which handles the decoded message.
  void (*dispatch)(void* interface, void* bytes, ::fidl::Transaction* txn);
};

// The compiler generates an array of MethodEntry for each protocol.
// The TryDispatch method for each protocol calls this function using the generated entries, which
// searches through the array using the method ordinal to find the corresponding dispatch function.
::fidl::DispatchResult TryDispatch(void* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn,
                                   MethodEntry* begin, MethodEntry* end);

}  // namespace internal

// This class manages a server connection and its binding to an
// async_dispatcher_t* (which may be multi-threaded). See the detailed
// documentation on the |BindServer()| APIs below.
template <typename Protocol>
class ServerBindingRef {
 public:
  ~ServerBindingRef() = default;

  ServerBindingRef(ServerBindingRef&&) = default;
  ServerBindingRef& operator=(ServerBindingRef&&) = default;

  ServerBindingRef(const ServerBindingRef&) = default;
  ServerBindingRef& operator=(const ServerBindingRef&) = default;

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
    if (auto binding = event_sender_.binding_.lock())
      binding->Unbind(std::move(binding));
  }

  // Triggers an asynchronous unbind operation. Eventually, the epitaph will be sent over the
  // channel which will be subsequently closed. If specified, |on_unbound| will be invoked giving
  // the unbind reason as an argument.
  //
  // This may be called from any thread.
  void Close(zx_status_t epitaph) {
    if (auto binding = event_sender_.binding_.lock())
      binding->Close(std::move(binding), epitaph);
  }

  // Return the interface for sending FIDL events. If the server has been unbound, calls on the
  // interface return error with status ZX_ERR_CANCELED.
  const typename Protocol::WeakEventSender* get() const { return &event_sender_; }
  const typename Protocol::WeakEventSender* operator->() const { return &event_sender_; }
  const typename Protocol::WeakEventSender& operator*() const { return event_sender_; }

 private:
  // This is so that only |BindServerTypeErased| will be able to construct a
  // new instance of |ServerBindingRef|.
  friend fit::result<ServerBindingRef<Protocol>, zx_status_t>
  internal::BindServerTypeErased<Protocol>(async_dispatcher_t* dispatcher,
                                           fidl::ServerEnd<Protocol> server_end,
                                           internal::IncomingMessageDispatcher* interface,
                                           internal::AnyOnUnboundFn on_unbound);

  explicit ServerBindingRef(std::weak_ptr<internal::AsyncServerBinding<Protocol>> internal_binding)
      : event_sender_(std::move(internal_binding)) {}

  typename Protocol::WeakEventSender event_sender_;
};

namespace internal {

//
// Definitions related to binding a connection to a dispatcher
//

// Binds an implementation of some FIDL server protocol |interface| and
// |server_end| to the |dispatcher|.
//
// |interface| should be a pointer to some |Protocol::Interface| class.
//
// |IncomingMessageDispatcher::dispatch_message| looks up an incoming FIDL
// message in the associated protocol and possibly invokes a handler on
// |interface|, which will be provided as the first argument.
//
// |on_unbound| will be called with |interface| if |on_unbound| is specified.
// The public |fidl::BindServer| functions should translate |interface| back to
// the user pointer type, possibly at an offset, before invoking the
// user-provided on-unbound handler.
template <typename Protocol>
fit::result<ServerBindingRef<Protocol>, zx_status_t> BindServerTypeErased(
    async_dispatcher_t* dispatcher, fidl::ServerEnd<Protocol> server_end,
    IncomingMessageDispatcher* interface, internal::AnyOnUnboundFn on_unbound) {
  auto internal_binding = internal::AsyncServerBinding<Protocol>::Create(
      dispatcher, std::move(server_end), interface, std::move(on_unbound));
  auto status = internal_binding->BeginWait();
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(fidl::ServerBindingRef<Protocol>(std::move(internal_binding)));
}

// All overloads of |BindServer| calls into this function.
// This function exists to support deducing the |OnUnbound| type,
// and type-erasing the interface and the |on_unbound| handlers, before
// calling into |BindServerTypeErased|.
//
// Note: if you see a compiler error that ends up in this function, that is
// probably because you passed in an incompatible |on_unbound| handler.
template <typename ServerImpl, typename OnUnbound>
fit::result<ServerBindingRef<typename ServerImpl::_EnclosingProtocol>, zx_status_t> BindServerImpl(
    async_dispatcher_t* dispatcher,
    fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end, ServerImpl* impl,
    OnUnbound&& on_unbound) {
  using ProtocolType = typename ServerImpl::_EnclosingProtocol;
  return BindServerTypeErased<ProtocolType>(
      dispatcher, std::move(server_end), impl,
      [on_unbound = std::forward<OnUnbound>(on_unbound)](
          internal::IncomingMessageDispatcher* any_interface, UnbindInfo info,
          zx::channel channel) mutable {
        // Note: this cast may change the value of the pointer, due to how C++
        // implements classes with virtual tables.
        auto* impl = static_cast<ServerImpl*>(any_interface);
        std::invoke(on_unbound, impl, info, fidl::ServerEnd<ProtocolType>(std::move(channel)));
      });
}

template <typename OnUnbound>
using OnUnboundIsNull = std::is_same<std::remove_reference_t<OnUnbound>, std::nullptr_t>;

// This base class provides either a functioning `operator()` or a no-op,
// depending on whether the |OnUnbound| type is a nullptr.
template <typename Derived, typename OnUnbound, typename Enable = void>
struct UnboundThunkCallOperator;

template <typename Derived, typename OnUnbound>
struct UnboundThunkCallOperator<Derived, OnUnbound,
                                std::enable_if_t<!OnUnboundIsNull<OnUnbound>::value>> {
  template <typename ServerImpl>
  void operator()(ServerImpl* impl_ptr, UnbindInfo info,
                  fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol>&& server_end) {
    static_assert(std::is_convertible_v<OnUnbound, OnUnboundFn<ServerImpl>>,
                  "|on_unbound| must have the same signature as fidl::OnUnboundFn<ServerImpl>.");
    auto* self = static_cast<Derived*>(this);
    std::invoke(self->on_unbound_, impl_ptr, info, std::move(server_end));
  }
};

template <typename Derived, typename OnUnbound>
struct UnboundThunkCallOperator<Derived, OnUnbound,
                                std::enable_if_t<OnUnboundIsNull<OnUnbound>::value>> {
  template <typename ServerImpl>
  void operator()(ServerImpl* impl_ptr, UnbindInfo info,
                  fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol>&& server_end) {
    // |fn_| is a nullptr, meaning the user did not provide an |on_unbound| callback.
    static_assert(std::is_same_v<OnUnbound, std::nullptr_t>, "|on_unbound| is no-op here");
  }
};

// An |UnboundThunk| is a functor that delegates to an |OnUnbound| callable,
// and which ensures that the server implementation is only destroyed after
// the invocation and destruction of the |OnUnbound| callable, when the server
// is managed in a |shared_ptr| or |unique_ptr|.
template <typename ServerImplMaybeOwned, typename OnUnbound>
struct UnboundThunk
    : public UnboundThunkCallOperator<UnboundThunk<ServerImplMaybeOwned, OnUnbound>, OnUnbound> {
  UnboundThunk(ServerImplMaybeOwned&& impl, OnUnbound&& on_unbound)
      : impl_(std::forward<ServerImplMaybeOwned>(impl)),
        on_unbound_(std::forward<OnUnbound>(on_unbound)) {}

  std::remove_reference_t<ServerImplMaybeOwned> impl_;
  std::remove_reference_t<OnUnbound> on_unbound_;
};

}  // namespace internal

// Binds an implementation of a low-level C++ server interface to |server_end| using a potentially
// multi-threaded |dispatcher|. This implementation allows for multiple in-flight synchronously or
// asynchronously handled transactions.
//
// This function adds an asynchronous wait to the given |dispatcher| for new messages to arrive on
// |server_end|. When a message arrives, the dispatch function corresponding to the interface is
// called on one of the |dispatcher| threads.
//
// Typically, the dispatch function is generated by the low-level C++ backend for FIDL interfaces.
// These dispatch functions decode the |fidl_incoming_msg_t| and call into the implementation of the
// FIDL interface, via its C++ vtable.
//
// Creation:
// - Upon success |BindServer| creates a binding that owns |server_end|. In this case, the binding
//   is initially kept alive even if the returned fit::result with a |ServerBindingRef| is ignored.
// - |ServerBindingRef| is a reference to the binding, it does not hold the binding. To unbind the
//   binding, see Unbind below.
// - Upon any error creating the binding, |BindServer| returns a fit::error and |server_end| is
//   closed.
//
// Destruction:
// - If the returned |ServerBindingRef| is ignored or dropped some time during the server operation,
//   then if some error occurs (see below) the binding will be automatically destroyed.
// - If the returned |ServerBindingRef| is kept but an error occurs (see below), the binding will be
//   destroyed, though calls may still be made on the |ServerBindingRef|.
// - On an error, |server_end| is unbound from the dispatcher, i.e. no dispatcher threads will
//   interact with it. Calls on inflight |Transaction|s will have no effect. If |on_unbound| is not
//   specified, the |server_end| is closed. If specified, |on_unbound| is then executed on a
//   |dispatcher| thread allowing the user to process the error. |on_unbound| includes the server
//   end of the channel as a parameter, if ignored the server-end will be closed at the end of
//   |on_unbound|'s scope.
//
// Unbind:
// - The |ServerBindingRef| can be used to explicitly |Unbind| the binding and retrieve the
//   |server_end| endpoint.
// - |Unbind| is non-blocking with respect to user code paths, i.e. if it blocks, it does so on
//   deterministic internal code paths. As such, the user may safely synchronize around an |Unbind|
//   call.
// - In order to reclaim the |server_end|, the user must specify an |on_unbound| hook. This will be
//   invoked after the |server_end| has been unbound from the |dispatcher|. The |server_end| will be
//   given as an argument to the hook.
// - If the user shuts down the |dispatcher| prior to the |on_unbound| hook running, it may be
//   dropped instead.
//
// Close:
// - |Close| is similar to |Unbind| except that it causes an epitaph message to be sent on the
//   |server_end|.
// - If specified, the |on_unbound| hook will execute after the epitaph has been sent and like in
//   |Unbind| the |server_end| will be given as an argument to the hook and if unused it will be
//   closed at the end of the hook scope.
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
// - To enable more concurrency, the user may invoke |EnableNextDispatch| on the
//   |fidl::Completer<T>::Sync| from the dispatch function. This will resume the async wait on the
//   |dispatcher| before the dispatch function returns, allowing other |dispatcher| threads to
//   handle messages for the same binding.
// NOTE: If a particular user does not care about ordering, they may invoke |EnableNextDispatch|
// immediately in the message handler. However, this functionality could instead be provided as a
// default configuration. If you have such a usecase, please contact madhaviyengar@ or yifeit@.
//
// The following |BindServer()| APIs infer the protocol type based on the server implementation
// which must publicly inherit from the appropriate |<Protocol_Name>::Interface| class.
// The following code would compile without explicitly specializing
// |BindServer|:
//
//     // Suppose |this| is a server implementation class |Foo|, that
//     // implements the |Bar| FIDL protocol.
//     fidl:ServerEnd<Bar> server_end = ...;
//     fidl::BindServer(dispatcher, std::move(server_end), this);
//
// TODO(fxbug.dev/67062): |fidl::BindServer| and associated API should return a |zx::status|.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
fit::result<ServerBindingRef<typename ServerImpl::_EnclosingProtocol>, zx_status_t> BindServer(
    async_dispatcher_t* dispatcher,
    fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end, ServerImpl* impl,
    OnUnbound&& on_unbound = nullptr) {
  return internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl,
      internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// Overload of |BindServer| that takes ownership of the server as a |unique_ptr|.
// The pointer is destroyed on the same thread as the one calling |on_unbound|,
// and happens right after |on_unbound|. See |BindServer| for details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
fit::result<ServerBindingRef<typename ServerImpl::_EnclosingProtocol>, zx_status_t> BindServer(
    async_dispatcher_t* dispatcher,
    fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    std::unique_ptr<ServerImpl>&& impl, OnUnbound&& on_unbound = nullptr) {
  ServerImpl* impl_raw = impl.get();
  return internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl_raw,
      internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// Overload of |BindServer| that shares ownership of the server via a |shared_ptr|.
// The pointer is destroyed on the same thread as the one calling |on_unbound|,
// and happens right after |on_unbound|. See |BindServer| for details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
fit::result<ServerBindingRef<typename ServerImpl::_EnclosingProtocol>, zx_status_t> BindServer(
    async_dispatcher_t* dispatcher,
    fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    std::shared_ptr<ServerImpl> impl, OnUnbound&& on_unbound = nullptr) {
  ServerImpl* impl_raw = impl.get();
  return internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl_raw,
      internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_H_
