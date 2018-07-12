// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDING_H_
#define LIB_FIDL_CPP_BINDING_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <memory>
#include <utility>

#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/internal/stub_controller.h"

namespace fidl {

// Binds the implementation of |Interface| to a channel.
//
// The |Binding| listens for incoming messages on the channel, decodes them, and
// calls the appropriate method on the bound implementation. If the message
// expects a reply, the |Binding| creates callbacks that encode and send
// reply messages when called.
//
// When the |Binding| object is destroyed, the binding between the channel
// and the interface is torn down and the channel is closed, leaving the
// |Binding| in an unbound state.
//
// The implementation pointer type of the binding is also parameterized,
// allowing the use of smart pointer types such as |std::unique_ptr| to
// reference the implementation.
//
// Example:
//
//   #include "foo.fidl.h"
//
//   class FooImpl : public Foo {
//    public:
//     explicit FooImpl(InterfaceRequest<Foo> request)
//         : binding_(this, std::move(request) {}
//
//     // Foo implementation here.
//
//    private:
//     Binding<Foo> binding_;
//   };
//
// After the |Binding| has been bound to an implementation, the implementation
// will receive methods calls from the remote endpoint of the channel on the
// async_t on which the |InterfaceRequest| was bound. By default this is the
// thread on which the binding occurred.
//
// See also:
//
//  * |InterfacePtr|, which is the client analog of a |Binding|.
template <typename Interface, typename ImplPtr = Interface*>
class Binding {
 public:
  // Constructs an incomplete binding that will use the implementation |impl|.
  //
  // The binding may be completed with a subsequent call to the |Bind| method.
  // Does not take ownership of |impl|, which must outlive the binding.
  explicit Binding(ImplPtr impl)
      : impl_(std::forward<ImplPtr>(impl)), stub_(&*this->impl()) {
    controller_.set_stub(&stub_);
    stub_.set_controller(&controller_);
  }

  // Constructs a completed binding of |channel| to implementation |impl|.
  //
  // Does not take ownership of |impl|, which must outlive the binding.
  //
  // If the |Binding| cannot be bound to the given |channel| (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|), the |Binding| will be constructed
  // in an unbound state.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  Binding(ImplPtr impl, zx::channel channel, async_dispatcher_t* dispatcher = nullptr)
      : Binding(std::forward<ImplPtr>(impl)) {
    Bind(std::move(channel), dispatcher);
  }

  // Constructs a completed binding of |impl| to the channel in |request|.
  //
  // Does not take ownership of |impl|, which must outlive the binding.
  //
  // If the |Binding| cannot be bound to the given |channel| (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|), the |Binding| will be constructed
  // in an unbound state.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  Binding(ImplPtr impl, InterfaceRequest<Interface> request,
          async_dispatcher_t* dispatcher = nullptr)
      : Binding(std::forward<ImplPtr>(impl)) {
    Bind(request.TakeChannel(), dispatcher);
  }

  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;

  // Completes a binding by creating a new channel, binding one endpoint to
  // the previously specified implementation and returning the other endpoint.
  //
  // If |NewBinding| fails to create the underlying channel, the returned
  // |InterfaceHandle| will return false from |is_valid()|.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  InterfaceHandle<Interface> NewBinding(async_dispatcher_t* dispatcher = nullptr) {
    InterfaceHandle<Interface> client;
    Bind(client.NewRequest().TakeChannel(), dispatcher);
    return client;
  }

  // Binds the previously specified implementation to the given |channel|.
  //
  // If the |Binding| was prevously bound to another channel, that channel is
  // closed.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  //
  // Returns an error if the binding was not able to be created (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|).
  zx_status_t Bind(zx::channel channel, async_dispatcher_t* dispatcher = nullptr) {
    return controller_.reader().Bind(std::move(channel), dispatcher);
  }

  // Binds the previously specified implementation to the given
  // |InterfaceRequest|.
  //
  // If the |Binding| was prevously bound to another channel, that channel is
  // closed.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  //
  // Returns an error if the binding was not able to be created (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|).
  zx_status_t Bind(InterfaceRequest<Interface> request,
                   async_dispatcher_t* dispatcher = nullptr) {
    return Bind(request.TakeChannel(), dispatcher);
  }

  // Unbinds the underlying channel from this binding and returns it so it can
  // be used in another context, such as on another thread or with a different
  // implementation.
  //
  // After this function returns, the |Binding| is ready to be bound to another
  // channel.
  InterfaceRequest<Interface> Unbind() {
    return InterfaceRequest<Interface>(controller_.reader().Unbind());
  }

  // Blocks the calling thread until either a message arrives on the previously
  // bound channel or an error occurs.
  //
  // Returns an error if waiting for the message, reading the message, or
  // processing the message fails. If the error results in the channel being
  // closed, the error handler will be called synchronously before this
  // method returns.
  //
  // This method can be called only if this |Binding| is currently bound to a
  // channel.
  zx_status_t WaitForMessage() {
    return controller_.reader().WaitAndDispatchOneMessageUntil(
        zx::time::infinite());
  }

  // Sets an error handler that will be called if an error causes the
  // underlying channel to be closed.
  //
  // For example, the error handler will be called if the remote side of the
  // channel sends an invalid message. When the error handler is called, the
  // |Binding| will no longer be bound to the channel.
  void set_error_handler(fit::closure error_handler) {
    controller_.reader().set_error_handler(std::move(error_handler));
  }

  // The implementation used by this |Binding| to process incoming messages.
  const ImplPtr& impl() const { return impl_; }

  // The interface for sending events back to the client.
  typename Interface::EventSender_& events() { return stub_; }

  // Whether this |Binding| is currently listening to a channel.
  bool is_bound() const { return controller_.reader().is_bound(); }

  // The underlying channel.
  const zx::channel& channel() const { return controller_.reader().channel(); }

 private:
  const ImplPtr impl_;
  typename Interface::Stub_ stub_;
  internal::StubController controller_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDING_H_
