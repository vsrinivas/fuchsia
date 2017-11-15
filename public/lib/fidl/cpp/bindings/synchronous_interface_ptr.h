// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_SYNCHRONOUS_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_BINDINGS_SYNCHRONOUS_INTERFACE_PTR_H_

#include <cstddef>
#include <memory>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/synchronous_connector.h"
#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fidl/cpp/bindings/message_validator.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace fidl {

// A synchronous version of InterfacePtr. Interface message calls using a
// SynchronousInterfacePtr will block if a response message is expected. This
// class uses the generated synchronous versions of the mojo interfaces.
//
// To make a SynchronousInterfacePtr, use the |Create()| factory method and
// supply the InterfaceHandle to it. Use |PassInterfaceHandle()| to extract the
// InterfaceHandle.
//
// SynchronousInterfacePtr is thread-compatible (but not thread-safe).
//
// TODO(vardhan): Add support for InterfaceControlMessage methods.
// TODO(vardhan): Message calls invoked on this class will return |false| if
// there are any channel errors.  Should there be a better way to expose
// underlying channel errors?
template <typename Interface>
class SynchronousInterfacePtr {
 public:
  // Constructs an unbound SynchronousInterfacePtr.
  SynchronousInterfacePtr() {}
  SynchronousInterfacePtr(std::nullptr_t) {}

  // Takes over the binding of another SynchronousInterfacePtr, and closes any
  // channel already bound to this pointer.
  SynchronousInterfacePtr(SynchronousInterfacePtr&& other) = default;

  // Takes over the binding of another SynchronousInterfacePtr, and closes any
  // channel already bound to this pointer.
  SynchronousInterfacePtr& operator=(SynchronousInterfacePtr&& other) = default;

  static SynchronousInterfacePtr<Interface> Create(
      InterfaceHandle<Interface> handle) {
    return SynchronousInterfacePtr<Interface>(std::move(handle));
  }

  // Closes the bound channel (if any).
  void reset() { *this = SynchronousInterfacePtr<Interface>(); }

  // Returns a raw pointer to the local proxy. Caller does not take ownership.
  // Note that the local proxy is thread hostile, as stated above.
  typename Interface::Synchronous_* get() { return proxy_.get(); }

  typename Interface::Synchronous_* operator->() {
    FXL_DCHECK(connector_);
    FXL_DCHECK(proxy_);
    return proxy_.get();
  }
  typename Interface::Synchronous_& operator*() { return *operator->(); }

  // Returns whether or not this SynchronousInterfacePtr is bound to a channel.
  bool is_bound() const { return connector_ && connector_->is_valid(); }
  explicit operator bool() const { return is_bound(); }

  // Unbinds the SynchronousInterfacePtr and returns the underlying
  // InterfaceHandle for the interface.
  InterfaceHandle<Interface> PassInterfaceHandle() {
    InterfaceHandle<Interface> handle(connector_->PassHandle());
    reset();
    return handle;
  }

 private:
  // A simple I/O interface we supply to the generated |proxy_| so it doesn't
  // have to know how to write mojo message.
  std::unique_ptr<internal::SynchronousConnector> connector_;
  // |proxy_| must outlive |connector_|, so make sure it is declared in this
  // order.
  std::unique_ptr<typename Interface::Synchronous_::Proxy_> proxy_;

  SynchronousInterfacePtr(InterfaceHandle<Interface> handle) {
    connector_.reset(new internal::SynchronousConnector(handle.PassHandle()));

    fidl::internal::MessageValidatorList validators;
    validators.push_back(std::unique_ptr<fidl::internal::MessageValidator>(
        new fidl::internal::MessageHeaderValidator));
    validators.push_back(std::unique_ptr<fidl::internal::MessageValidator>(
        new typename Interface::ResponseValidator_));

    proxy_.reset(new typename Interface::Synchronous_::Proxy_(
        connector_.get(), std::move(validators)));
  }

  FIDL_MOVE_ONLY_TYPE(SynchronousInterfacePtr);
};

// Creates a new channel over which Interface is to be served. Binds the
// specified SynchronousInterfacePtr to one end of the channel, and returns
// an InterfaceRequest bound to the other. The SynchronousInterfacePtr should be
// passed to the client, and the InterfaceRequest should be passed to whatever
// will provide the implementation. Unlike InterfacePtr<>, invocations on
// SynchronousInterfacePtr<> will block until a response is received, so the
// user must pass off InterfaceRequest<> to an implementation before issuing any
// calls.
//
// Example:
// ========
// Given the following interface
//   interface Echo {
//     EchoString(string str) => (string value);
//   }
//
// The client would have code similar to the following:
//
//   SynchronousInterfacePtr<Echo> client;
//   InterfaceRequest<Echo> impl = GetSynchronousProxy(&client);
//   // .. pass |impl| off to an implementation.
//   fidl::String out;
//   client->EchoString("hello!", &out);
//
// TODO(vardhan): Consider renaming this function, along with her sister
// |GetProxy()| functions. Maybe `MakeSyncProxy()`?
template <typename Interface>
InterfaceRequest<Interface> GetSynchronousProxy(
    SynchronousInterfacePtr<Interface>* ptr) {
  InterfaceHandle<Interface> iface_handle;
  auto retval = GetProxy(&iface_handle);
  *ptr = SynchronousInterfacePtr<Interface>::Create(std::move(iface_handle));
  return retval;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_SYNCHRONOUS_INTERFACE_PTR_H_
