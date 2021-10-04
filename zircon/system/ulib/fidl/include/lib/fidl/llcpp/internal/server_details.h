// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_SERVER_DETAILS_H_
#define LIB_FIDL_LLCPP_INTERNAL_SERVER_DETAILS_H_

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/llcpp/server_end.h>

namespace fidl {

// Forward declarations.
template <typename Protocol>
class ServerBindingRef;

// |OnUnboundFn| can represent the callback which will be invoked after the
// server end of a channel is unbound from the dispatcher. See documentation on
// |BindServer| for details.
//
// It is not required to wrap the callback lambda in this type; |BindServer|
// accepts a lambda function directly.
template <typename ServerImpl>
using OnUnboundFn = fit::callback<void(
    ServerImpl*, UnbindInfo,
    typename fidl::Transport<typename ServerImpl::_EnclosingProtocol>::ServerEnd)>;

namespace internal {

//
// Definitions supporting the dispatch of a FIDL message
//

// The interface for dispatching incoming FIDL messages. The code generator
// will provide conforming implementations for relevant FIDL protocols.
class IncomingMessageDispatcher {
 public:
  virtual ~IncomingMessageDispatcher() = default;

  // Dispatches an incoming message to one of the handlers functions in the
  // protocol. If there is no matching handler, closes all the handles in
  // |msg| and initiates binding teardown.
  //
  // Note that the |dispatch_message| name avoids conflicts with FIDL method
  // names which would appear on the subclasses.
  //
  // Always consumes the handles in |msg|.
  virtual void dispatch_message(::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) = 0;
};

// Defines an incoming method entry. Used by a server to dispatch an incoming message.
struct MethodEntry {
  // The ordinal of the method handled by the entry.
  uint64_t ordinal;

  // The function which handles the encoded message.
  //
  // The function must consume the handles in |msg|.
  // The function should perform decoding, and return the decoding status.
  zx_status_t (*dispatch)(void* interface, ::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn);
};

// The compiler generates an array of MethodEntry for each protocol.
// The TryDispatch method for each protocol calls this function using the generated entries, which
// searches through the array using the method ordinal to find the corresponding dispatch function.
::fidl::DispatchResult TryDispatch(void* impl, ::fidl::IncomingMessage& msg,
                                   ::fidl::Transaction* txn, const MethodEntry* begin,
                                   const MethodEntry* end);

// Similar to |TryDispatch|, but closes all the handles in |msg| and notifies
// |txn| of an error in case of an unknown FIDL method.
void Dispatch(void* impl, ::fidl::IncomingMessage& msg, ::fidl::Transaction* txn,
              const MethodEntry* begin, const MethodEntry* end);

// The common bits in a weak event sender, i.e. an event sender that allows the
// transport to be destroyed from underneath it.
//
// This class is related to |AsyncTransaction|, but the latter has an special
// optimization for synchronous server method handlers, where it keeps a strong
// reference to the binding by default and does not need weak pointer promotion.
class WeakEventSenderInner {
 public:
  explicit WeakEventSenderInner(std::weak_ptr<::fidl::internal::AsyncServerBinding>&& binding)
      : binding_(std::move(binding)) {}

  // Sends an event.
  //
  // |message| will have its transaction ID set to zero.
  //
  // Errors are returned to the caller.
  fidl::Result SendEvent(::fidl::OutgoingMessage& message) const;

  const std::weak_ptr<::fidl::internal::AsyncServerBinding>& binding() const { return binding_; }

 private:
  // Handles errors in sending events. This may lead to binding teardown.
  void HandleSendError(fidl::Result error) const;

  std::weak_ptr<::fidl::internal::AsyncServerBinding> binding_;
};

//
// Definitions related to binding a connection to a dispatcher
//

// Binds an implementation of some FIDL server protocol |interface| and
// |server_end| to the |dispatcher|.
//
// |interface| should be a pointer to some |fidl::WireServer<Protocol>| class.
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
ServerBindingRef<Protocol> BindServerTypeErased(async_dispatcher_t* dispatcher,
                                                typename Transport<Protocol>::ServerEnd server_end,
                                                IncomingMessageDispatcher* interface,
                                                internal::AnyOnUnboundFn on_unbound) {
  auto internal_binding = internal::AsyncServerBinding::Create(
      dispatcher, Transport<Protocol>::TakeTransportObject(server_end), interface,
      std::move(on_unbound));
  auto binding_ref = fidl::ServerBindingRef<Protocol>(internal_binding);
  auto* binding_ptr = internal_binding.get();
  // The binding object keeps itself alive until unbinding, so dropping the
  // shared pointer here is fine.
  internal_binding.reset();
  binding_ptr->BeginFirstWait();
  return binding_ref;
}

// All overloads of |BindServer| calls into this function.
// This function exists to support deducing the |OnUnbound| type,
// and type-erasing the interface and the |on_unbound| handlers, before
// calling into |BindServerTypeErased|.
//
// Note: if you see a compiler error that ends up in this function, that is
// probably because you passed in an incompatible |on_unbound| handler.
template <typename ServerImpl, typename OnUnbound>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServerImpl(
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
  void operator()(
      ServerImpl* impl_ptr, UnbindInfo info,
      typename fidl::Transport<typename ServerImpl::_EnclosingProtocol>::ServerEnd&& server_end) {
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
  void operator()(
      ServerImpl* impl_ptr, UnbindInfo info,
      typename fidl::Transport<typename ServerImpl::_EnclosingProtocol>::ServerEnd&& server_end) {
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

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_SERVER_DETAILS_H_
