// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_INTERFACE_PTR_INTERNAL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_INTERFACE_PTR_INTERNAL_H_

#include <algorithm>  // For |std::swap()|.
#include <memory>
#include <utility>
#include <functional>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/router.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

struct FidlAsyncWaiter;

namespace fidl {
namespace internal {

template <typename Interface>
class InterfacePtrState {
 public:
  InterfacePtrState()
      : proxy_(nullptr), router_(nullptr), waiter_(nullptr), version_(0u) {}

  ~InterfacePtrState() {
    // Destruction order matters here. We delete |proxy_| first, even though
    // |router_| may have a reference to it, so that destructors for any request
    // callbacks still pending can interact with the InterfacePtr.
    delete proxy_;
    delete router_;
  }

  Interface* instance() {
    ConfigureProxyIfNecessary();

    // This will be null if the object is not bound.
    return proxy_;
  }

  uint32_t version() const { return version_; }

  void Swap(InterfacePtrState* other) {
    using std::swap;
    swap(other->proxy_, proxy_);
    swap(other->router_, router_);
    handle_.swap(other->handle_);
    swap(other->waiter_, waiter_);
    swap(other->version_, version_);
  }

  void Bind(InterfaceHandle<Interface> info, const FidlAsyncWaiter* waiter) {
    FXL_DCHECK(!proxy_);
    FXL_DCHECK(!router_);
    FXL_DCHECK(!(bool)handle_);
    FXL_DCHECK(!waiter_);
    FXL_DCHECK(version_ == 0u);
    FXL_DCHECK(info.is_valid());

    handle_ = info.PassHandle();
    waiter_ = waiter;
    version_ = info.version();
  }

  bool WaitForIncomingResponse(fxl::TimeDelta timeout = fxl::TimeDelta::Max()) {
    ConfigureProxyIfNecessary();

    FXL_DCHECK(router_);
    return router_->WaitForIncomingMessage(timeout);
  }

  // After this method is called, the object is in an invalid state and
  // shouldn't be reused.
  InterfaceHandle<Interface> PassInterfaceHandle() {
    return InterfaceHandle<Interface>(
        router_ ? router_->PassChannel() : std::move(handle_), version_);
  }

  bool is_bound() const { return (bool)handle_ || router_; }

  bool encountered_error() const {
    return router_ ? router_->encountered_error() : false;
  }

  void set_connection_error_handler(fxl::Closure error_handler) {
    ConfigureProxyIfNecessary();

    FXL_DCHECK(router_);
    router_->set_connection_error_handler(std::move(error_handler));
  }

  Router* router_for_testing() {
    ConfigureProxyIfNecessary();
    return router_;
  }

 private:
  using Proxy = typename Interface::Proxy_;

  void ConfigureProxyIfNecessary() {
    // The proxy has been configured.
    if (proxy_) {
      FXL_DCHECK(router_);
      return;
    }
    // The object hasn't been bound.
    if (!waiter_) {
      FXL_DCHECK(!(bool)handle_);
      return;
    }

    MessageValidatorList validators;
    validators.push_back(
        std::unique_ptr<MessageValidator>(new MessageHeaderValidator));
    validators.push_back(std::unique_ptr<MessageValidator>(
        new typename Interface::ResponseValidator_));

    router_ = new Router(std::move(handle_), std::move(validators), waiter_);
    waiter_ = nullptr;

    proxy_ = new Proxy(router_);
  }

  Proxy* proxy_;
  Router* router_;

  // |proxy_| and |router_| are not initialized until read/write with the
  // channel handle is needed. |handle_| and |waiter_| are valid between
  // the Bind() call and the initialization of |proxy_| and |router_|.
  mx::channel handle_;
  const FidlAsyncWaiter* waiter_;

  uint32_t version_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InterfacePtrState);
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_INTERFACE_PTR_INTERNAL_H_
