// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_REQUEST_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_REQUEST_H_

#include <zx/channel.h>

#include <cstddef>
#include <utility>

#include "lib/fidl/cpp/bindings/macros.h"

namespace fidl {

template <typename I>
class InterfacePtr;

template <typename I>
class InterfaceHandle;

// Represents a request from a remote client for an implementation of Interface
// over a specified channel. The implementor of the interface should
// remove the channel by calling TakeChannel() and bind it to the
// implementation. If this is not done, the InterfaceRequest will automatically
// close the channel on destruction. Can also represent the absence of a request
// if the client did not provide a channel.
template <typename Interface>
class InterfaceRequest {
 public:
  // Constructs an "empty" InterfaceRequest, representing that the client is not
  // requesting an implementation of Interface.
  InterfaceRequest() {}
  InterfaceRequest(std::nullptr_t) {}

  // Constructs an InterfaceRequest from a channel handle (if |handle| is
  // not set, then this constructs an "empty" InterfaceRequest).
  explicit InterfaceRequest(zx::channel handle) : handle_(std::move(handle)) {}

  // Takes the channel from another InterfaceRequest.
  InterfaceRequest(InterfaceRequest&& other) {
    handle_ = std::move(other.handle_);
  }
  InterfaceRequest& operator=(InterfaceRequest&& other) {
    handle_ = std::move(other.handle_);
    return *this;
  }

  // Binds the request to a channel over which Interface is to be
  // requested.  If the request is already bound to a channel, the current
  // channel will be closed.
  const zx::channel& channel() const { return handle_; }
  void set_channel(zx::channel handle) { handle_ = std::move(handle); }

  // Indicates whether the request currently contains a valid channel.
  bool is_valid() const { return !!handle_; }

  // Tests as true if pending, false if not.
  explicit operator bool() const { return is_valid(); }

  // Removes the channel from the request and returns it.
  zx::channel TakeChannel() { return std::move(handle_); }

  // TODO(abarth): Remove once clients are gone.
  zx::channel PassChannel() { return std::move(handle_); }

 private:
  zx::channel handle_;

  FIDL_MOVE_ONLY_TYPE(InterfaceRequest);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_REQUEST_H_
