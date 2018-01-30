// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_HANDLE_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_HANDLE_H_

#include <zircon/assert.h>
#include <zx/channel.h>

#include <cstddef>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/macros.h"

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
  InterfaceHandle() {}
  InterfaceHandle(std::nullptr_t) {}

  InterfaceHandle(zx::channel handle)
      : handle_(std::move(handle)) {}

  InterfaceHandle(InterfaceHandle&& other)
      : handle_(std::move(other.handle_)) {}

  // Making this constructor templated ensures that it is not type-instantiated
  // unless it is used, making the InterfacePtr<->InterfaceHandle codependency
  // less fragile.
  template <typename SameInterfaceAsAbove = Interface>
  InterfaceHandle(InterfacePtr<SameInterfaceAsAbove>&& ptr) {
    *this = ptr.Unbind();
  }

  // Making this constructor templated ensures that it is not type-instantiated
  // unless it is used, making the SynchronousInterfacePtr<->InterfaceHandle
  // codependency less fragile.
  template <typename SameInterfaceAsAbove = Interface>
  InterfaceHandle(SynchronousInterfacePtr<SameInterfaceAsAbove>&& ptr) {
    *this = ptr.Unbind();
  }

  ~InterfaceHandle() {}

  InterfaceHandle& operator=(InterfaceHandle&& other) {
    if (this != &other)
      handle_ = std::move(other.handle_);
    return *this;
  }

  // Creates a new pair of channels, one end bound to this InterfaceHandle<>,
  // and returns the other end as a InterfaceRequest<>. InterfaceRequest<>
  // should be passed to whatever will provide the implementation, and this
  // InterfaceHandle<> should be passed to whatever will construct a proxy to
  // the implementation (presumably using an InterfacePtr<> or equivalent).
  InterfaceRequest<Interface> NewRequest() {
    ZX_DEBUG_ASSERT(!is_valid());

    zx::channel request_endpoint;
    zx::channel::create(0, &handle_, &request_endpoint);
    return InterfaceRequest<Interface>(std::move(request_endpoint));
  }

  // Creates an |InterfacePtr| bound to the channel in this |InterfaceHandle|.
  //
  // This function transfers ownership of the underlying channel to the
  // returned |InterfacePtr|, which means the |is_valid()| method will return
  // false after this method returns.
  //
  // Requires the current thread to have a default async_t (e.g., a message
  // loop) in order to read messages from the channel and to monitor the
  // channel for |ZX_CHANNEL_PEER_CLOSED|.
  //
  // Making this method templated ensures that it is not type-instantiated
  // unless it is used, making the InterfacePtr<->InterfaceHandle codependency
  // less fragile.
  template <typename InterfacePtr = InterfacePtr<Interface>>
  inline InterfacePtr Bind() {
    InterfacePtr ptr;
    ptr.Bind(std::move(handle_));
    return ptr;
  }

  // Tests as true if we have a valid handle.
  explicit operator bool() const { return is_valid(); }
  bool is_valid() const { return !!handle_; }

  zx::channel TakeChannel() { return std::move(handle_); }
  const zx::channel& channel() const { return handle_; }
  void set_channel(zx::channel handle) { handle_ = std::move(handle); }

 private:
  zx::channel handle_;

  FIDL_MOVE_ONLY_TYPE(InterfaceHandle);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_HANDLE_H_
