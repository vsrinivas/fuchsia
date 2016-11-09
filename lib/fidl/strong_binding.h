// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_STRONG_BINDING_H_
#define APPS_MODULAR_LIB_FIDL_STRONG_BINDING_H_

#include <assert.h>
#include <mx/channel.h>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/router.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"

namespace modular {

// This connects an interface implementation strongly to a pipe. When a
// connection error is detected the implementation is deleted. Deleting the
// connector also closes the pipe.
//
// The strong binding does not actually take ownership of the implementation.
// If the implementation is not bound to a message pipe or if the message
// pipe is closed locally then no connection error will be produced and the
// implementation will therefore not be deleted by the strong binding.
// Keep this in mind to avoid unintentional memory leaks.
//
// Example of an implementation that is always bound strongly to a pipe
//
//   class StronglyBound : public Foo {
//    public:
//     explicit StronglyBound(InterfaceRequest<Foo> request)
//         : binding_(this, std::move(request)) {}
//
//     // Foo implementation here
//
//    private:
//     StrongBinding<Foo> binding_;
//   };
template <typename Interface, typename ImplPtr = Interface*>
class StrongBinding {
 public:
  // Constructs an incomplete binding that will use the implementation |impl|.
  // The binding may be completed with a subsequent call to the |Bind| method.
  // Does not take ownership of |impl|, which must outlive the binding.
  explicit StrongBinding(ImplPtr impl) : binding_(impl) {
    binding_.set_connection_error_handler([this]() { OnConnectionError(); });
  }

  // Constructs a completed binding of message pipe |handle| to implementation
  // |impl|. Does not take ownership of |impl|, which must outlive the binding.
  // See class comment for definition of |waiter|.
  StrongBinding(ImplPtr impl,
                mx::channel handle,
                const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter())
      : StrongBinding(impl) {
    binding_.Bind(std::move(handle), waiter);
  }

  // Constructs a completed binding of |impl| to a new message pipe, passing the
  // client end to |ptr|, which takes ownership of it. The caller is expected to
  // pass |ptr| on to the client of the service. Does not take ownership of any
  // of the parameters. |impl| must outlive the binding. |ptr| only needs to
  // last until the constructor returns. See class comment for definition of
  // |waiter|.
  StrongBinding(ImplPtr impl,
                fidl::InterfaceHandle<Interface>* ptr,
                const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter())
      : StrongBinding(impl) {
    binding_.Bind(ptr, waiter);
  }

  // Constructs a completed binding of |impl| to the message pipe endpoint in
  // |request|, taking ownership of the endpoint. Does not take ownership of
  // |impl|, which must outlive the binding. See class comment for definition of
  // |waiter|.
  StrongBinding(ImplPtr impl,
                fidl::InterfaceRequest<Interface> request,
                const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter())
      : StrongBinding(impl) {
    binding_.Bind(std::move(request), waiter);
  }

  // Tears down the binding, closing the message pipe and leaving the interface
  // implementation unbound.  Does not cause the implementation to be deleted.
  ~StrongBinding() {}

  // Completes a binding that was constructed with only an interface
  // implementation. Takes ownership of |handle| and binds it to the previously
  // specified implementation. See class comment for definition of |waiter|.
  void Bind(mx::channel handle,
            const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter()) {
    assert(!binding_.is_bound());
    binding_.Bind(std::move(handle), waiter);
  }

  // Completes a binding that was constructed with only an interface
  // implementation by creating a new message pipe, binding one end of it to the
  // previously specified implementation, and passing the other to |ptr|, which
  // takes ownership of it. The caller is expected to pass |ptr| on to the
  // eventual client of the service. Does not take ownership of |ptr|. See
  // class comment for definition of |waiter|.
  void Bind(fidl::InterfaceHandle<Interface>* ptr,
            const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter()) {
    assert(!binding_.is_bound());
    binding_.Bind(ptr, waiter);
  }

  // Completes a binding that was constructed with only an interface
  // implementation by removing the message pipe endpoint from |request| and
  // binding it to the previously specified implementation. See class comment
  // for definition of |waiter|.
  void Bind(fidl::InterfaceRequest<Interface> request,
            const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter()) {
    assert(!binding_.is_bound());
    binding_.Bind(std::move(request), waiter);
  }

  // Blocks the calling thread until either a call arrives on the previously
  // bound message pipe, the deadline is exceeded, or an error occurs. Returns
  // true if a method was successfully read and dispatched.
  bool WaitForIncomingMethodCall(
      ftl::TimeDelta timeout = ftl::TimeDelta::Max()) {
    return binding_.WaitForIncomingMethodCall(timeout);
  }

  // Closes the message pipe that was previously bound.  Does not cause the
  // implementation to be deleted.
  void Close() { binding_.Close(); }

  // Unbinds the underlying pipe from this binding and returns it so it can be
  // used in another context, such as on another thread or with a different
  // implementation. Put this object into a state where it can be rebound to a
  // new pipe.  Does not cause the implementation to be deleted.
  fidl::InterfaceRequest<Interface> Unbind() { return binding_.Unbind(); }

  // Sets an error handler that will be called if a connection error occurs on
  // the bound message pipe.  Note: The error handler must not delete the
  // interface implementation since that will happen immediately after the
  // error handler returns.
  void set_connection_error_handler(const ftl::Closure& error_handler) {
    connection_error_handler_ = error_handler;
  }

  // Returns the interface implementation that was previously specified. Caller
  // does not take ownership.
  Interface* impl() { return binding_.impl(); }

  // Indicates whether the binding has been completed (i.e., whether a message
  // pipe has been bound to the implementation).
  bool is_bound() const { return binding_.is_bound(); }

  // Returns the value of the handle currently bound to this Binding which can
  // be used to make explicit Wait/WaitMany calls. Requires that the Binding be
  // bound. Ownership of the handle is retained by the Binding, it is not
  // transferred to the caller.
  mx_handle_t handle() const { return binding_.handle(); }

  // Exposed for testing, should not generally be used.
  fidl::internal::Router* internal_router() {
    return binding_.internal_router();
  }

  void OnConnectionError() {
    if (connection_error_handler_) {
      connection_error_handler_();
    }
    delete binding_.impl();
  }

 private:
  ftl::Closure connection_error_handler_;
  fidl::Binding<Interface> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StrongBinding);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_STRONG_BINDING_H_
