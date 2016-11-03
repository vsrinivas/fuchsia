// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_BINDING_H_
#define LIB_FIDL_CPP_BINDINGS_BINDING_H_

#include <mx/channel.h>

#include <memory>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/router.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"

namespace fidl {

// Represents the binding of an interface implementation to a message pipe.
// When the |Binding| object is destroyed, the binding between the message pipe
// and the interface is torn down and the message pipe is closed, leaving the
// interface implementation in an unbound state.
//
// Example:
//
//   #include "foo.mojom.h"
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
// The caller may specify a |FidlAsyncWaiter| to be used by the connection when
// waiting for calls to arrive. Normally it is fine to use the default waiter.
// However, the caller may provide their own implementation if needed. The
// |Binding| will not take ownership of the waiter, and the waiter must outlive
// the |Binding|. The provided waiter must be able to signal the implementation
// which generally means it needs to be able to schedule work on the thread the
// implementation runs on. If writing library code that has to work on different
// types of threads callers may need to provide different waiter
// implementations.
template <typename Interface>
class Binding {
 public:
  // Constructs an incomplete binding that will use the implementation |impl|.
  // The binding may be completed with a subsequent call to the |Bind| method.
  // Does not take ownership of |impl|, which must outlive the binding.
  explicit Binding(Interface* impl) : impl_(impl) { stub_.set_sink(impl_); }

  // Constructs a completed binding of message pipe |handle| to implementation
  // |impl|. Does not take ownership of |impl|, which must outlive the binding.
  // See class comment for definition of |waiter|.
  Binding(Interface* impl,
          mx::channel handle,
          const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter())
      : Binding(impl) {
    Bind(std::move(handle), waiter);
  }

  // Constructs a completed binding of |impl| to a new message pipe, passing the
  // client end to |ptr|, which takes ownership of it. The caller is expected to
  // pass |ptr| on to the client of the service. Does not take ownership of any
  // of the parameters. |impl| must outlive the binding. |ptr| only needs to
  // last until the constructor returns. See class comment for definition of
  // |waiter|.
  Binding(Interface* impl,
          InterfaceHandle<Interface>* interface_handle,
          const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter())
      : Binding(impl) {
    Bind(interface_handle, waiter);
  }

  // Constructs a completed binding of |impl| to the message pipe endpoint in
  // |request|, taking ownership of the endpoint. Does not take ownership of
  // |impl|, which must outlive the binding. See class comment for definition of
  // |waiter|.
  Binding(Interface* impl,
          InterfaceRequest<Interface> request,
          const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter())
      : Binding(impl) {
    Bind(request.PassMessagePipe(), waiter);
  }

  // Tears down the binding, closing the message pipe and leaving the interface
  // implementation unbound.
  ~Binding() {}

  // Completes a binding that was constructed with only an interface
  // implementation. Takes ownership of |handle| and binds it to the previously
  // specified implementation. See class comment for definition of |waiter|.
  void Bind(mx::channel handle,
            const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter()) {
    FTL_DCHECK(!internal_router_);

    internal::MessageValidatorList validators;
    validators.push_back(std::unique_ptr<internal::MessageValidator>(
        new internal::MessageHeaderValidator));
    validators.push_back(std::unique_ptr<internal::MessageValidator>(
        new typename Interface::RequestValidator_));

    internal_router_.reset(
        new internal::Router(std::move(handle), std::move(validators), waiter));
    internal_router_->set_incoming_receiver(&stub_);
    internal_router_->set_connection_error_handler([this]() {
      if (connection_error_handler_)
        connection_error_handler_();
    });
  }

  // Completes a binding that was constructed with only an interface
  // implementation by creating a new message pipe, binding one end of it to the
  // previously specified implementation, and passing the other to |ptr|, which
  // takes ownership of it. The caller is expected to pass |ptr| on to the
  // eventual client of the service. Does not take ownership of |ptr|. See
  // class comment for definition of |waiter|.
  void Bind(InterfaceHandle<Interface>* interface_handle,
            const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter()) {
    mx::channel endpoint0;
    mx::channel endpoint1;
    mx::channel::create(0, &endpoint0, &endpoint1);
    *interface_handle =
        InterfaceHandle<Interface>(std::move(endpoint0), Interface::Version_);
    Bind(std::move(endpoint1), waiter);
  }

  // Completes a binding that was constructed with only an interface
  // implementation by removing the message pipe endpoint from |request| and
  // binding it to the previously specified implementation. See class comment
  // for definition of |waiter|.
  void Bind(InterfaceRequest<Interface> request,
            const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter()) {
    Bind(request.PassMessagePipe(), waiter);
  }

  // Blocks the calling thread until either a call arrives on the previously
  // bound message pipe, the timeout is exceeded, or an error occurs. Returns
  // true if a method was successfully read and dispatched.
  bool WaitForIncomingMethodCall(
      ftl::TimeDelta timeout = ftl::TimeDelta::Max()) {
    FTL_DCHECK(internal_router_);
    return internal_router_->WaitForIncomingMessage(timeout);
  }

  // Closes the message pipe that was previously bound. Put this object into a
  // state where it can be rebound to a new pipe.
  void Close() {
    FTL_DCHECK(internal_router_);
    internal_router_.reset();
  }

  // Unbinds the underlying pipe from this binding and returns it so it can be
  // used in another context, such as on another thread or with a different
  // implementation. Put this object into a state where it can be rebound to a
  // new pipe.
  InterfaceRequest<Interface> Unbind() {
    auto request =
        InterfaceRequest<Interface>(internal_router_->PassMessagePipe());
    internal_router_.reset();
    return request;
  }

  // Sets an error handler that will be called if a connection error occurs on
  // the bound message pipe.
  void set_connection_error_handler(const ftl::Closure& error_handler) {
    connection_error_handler_ = error_handler;
  }

  // Returns the interface implementation that was previously specified. Caller
  // does not take ownership.
  Interface* impl() { return impl_; }

  // Indicates whether the binding has been completed (i.e., whether a message
  // pipe has been bound to the implementation).
  bool is_bound() const { return !!internal_router_; }

  // Returns the value of the handle currently bound to this Binding which can
  // be used to make explicit Wait/WaitMany calls. Requires that the Binding be
  // bound. Ownership of the handle is retained by the Binding, it is not
  // transferred to the caller.
  mx_handle_t handle() const {
    FTL_DCHECK(is_bound());
    return internal_router_->handle();
  }

  // Exposed for testing, should not generally be used.
  internal::Router* internal_router() { return internal_router_.get(); }

 private:
  std::unique_ptr<internal::Router> internal_router_;
  typename Interface::Stub_ stub_;
  Interface* impl_;
  ftl::Closure connection_error_handler_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Binding);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_BINDING_H_
