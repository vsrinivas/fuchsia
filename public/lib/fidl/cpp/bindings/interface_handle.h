// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_HANDLE_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_HANDLE_H_

#include <mx/channel.h>

#include <cstddef>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace fidl {

template <typename Interface>
class InterfacePtr;

template <typename Interface>
class SynchronousInterfacePtr;

// InterfaceHandle stores necessary information to communicate with a remote
// interface implementation, which could be used to construct an InterfacePtr.
template <typename Interface>
class InterfaceHandle {
 public:
  InterfaceHandle() : version_(Interface::Version_) {}
  InterfaceHandle(std::nullptr_t) : version_(Interface::Version_) {}

  InterfaceHandle(mx::channel handle, uint32_t version)
      : handle_(std::move(handle)), version_(version) {}

  InterfaceHandle(InterfaceHandle&& other)
      : handle_(std::move(other.handle_)), version_(other.version_) {
    other.version_ = 0u;
  }

  // Making this constructor templated ensures that it is not type-instantiated
  // unless it is used, making the InterfacePtr<->InterfaceHandle codependency
  // less fragile.
  template <typename SameInterfaceAsAbove = Interface>
  InterfaceHandle(InterfacePtr<SameInterfaceAsAbove>&& ptr) {
    *this = ptr.PassInterfaceHandle();
  }

  // Making this constructor templated ensures that it is not type-instantiated
  // unless it is used, making the SynchronousInterfacePtr<->InterfaceHandle
  // codependency less fragile.
  template <typename SameInterfaceAsAbove = Interface>
  InterfaceHandle(SynchronousInterfacePtr<SameInterfaceAsAbove>&& ptr) {
    *this = ptr.PassInterfaceHandle();
  }

  ~InterfaceHandle() {}

  InterfaceHandle& operator=(InterfaceHandle&& other) {
    if (this != &other) {
      handle_ = std::move(other.handle_);
      version_ = other.version_;
      other.version_ = 0u;
    }

    return *this;
  }

  // Creates a new pair of channels, one end bound to this InterfaceHandle<>,
  // and returns the other end as a InterfaceRequest<>. InterfaceRequest<>
  // should be passed to whatever will provide the implementation, and this
  // InterfaceHandle<> should be passed to whatever will construct a proxy to
  // the implementation (presumably using an InterfacePtr<> or equivalent).
  InterfaceRequest<Interface> NewRequest() {
    FXL_DCHECK(!is_valid()) << "An existing handle is already bound.";

    mx::channel request_endpoint;
    mx::channel::create(0, &handle_, &request_endpoint);
    return InterfaceRequest<Interface>(std::move(request_endpoint));
  }

  // Tests as true if we have a valid handle.
  explicit operator bool() const { return is_valid(); }
  bool is_valid() const { return !!handle_; }

  mx::channel PassHandle() { return std::move(handle_); }
  const mx::channel& handle() const { return handle_; }
  void set_handle(mx::channel handle) { handle_ = std::move(handle); }

  uint32_t version() const { return version_; }
  void set_version(uint32_t version) { version_ = version; }

 private:
  mx::channel handle_;
  uint32_t version_;

  FIDL_MOVE_ONLY_TYPE(InterfaceHandle);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_HANDLE_H_
