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
// remove the channel by calling PassChannel() and bind it to the
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

  // Assigning to nullptr resets the InterfaceRequest to an empty state,
  // closing the channel currently bound to it (if any).
  InterfaceRequest& operator=(std::nullptr_t) {
    handle_.reset();
    return *this;
  }

  // Binds the request to a channel over which Interface is to be
  // requested.  If the request is already bound to a channel, the current
  // channel will be closed.
  void Bind(zx::channel handle) { handle_ = std::move(handle); }

  // Indicates whether the request currently contains a valid channel.
  bool is_pending() const { return !!handle_; }

  // Tests as true if pending, false if not.
  explicit operator bool() const { return is_pending(); }

  // Removes the channel from the request and returns it.
  zx::channel PassChannel() { return std::move(handle_); }

 private:
  zx::channel handle_;

  FIDL_MOVE_ONLY_TYPE(InterfaceRequest);
};

// Creates a new channel over which Interface is to be served, and one end
// into the supplied |handle| and the returns the other inside an
// InterfaceRequest<>. The InterfaceHandle<> can be used to create a client
// proxy (such as an InterfacePtr<> or SynchronousInterfacePtr<>).
// InterfaceRequest<> should be passed to whatever will provide the
// implementation.
// TODO(vardhan): Rename this function? This isn't making a "proxy" you can use,
// as you still need to convert it into something like InterfacePtr<>.
template <typename Interface>
InterfaceRequest<Interface> GetProxy(InterfaceHandle<Interface>* handle) {
  zx::channel endpoint0;
  zx::channel endpoint1;
  zx::channel::create(0, &endpoint0, &endpoint1);
  *handle = InterfaceHandle<Interface>(std::move(endpoint0),
                                       Interface::Version_);
  return InterfaceRequest<Interface>(std::move(endpoint1));
}

// Creates a new channel over which Interface is to be served. Binds the
// specified InterfacePtr to one end of the channel, and returns an
// InterfaceRequest bound to the other. The InterfacePtr should be passed to
// the client, and the InterfaceRequest should be passed to whatever will
// provide the implementation. The implementation should typically be bound to
// the InterfaceRequest using the Binding or StrongBinding classes. The client
// may begin to issue calls even before an implementation has been bound, since
// messages sent over the channel will just queue up until they are consumed by
// the implementation.
//
// Example #1: Requesting a remote implementation of an interface.
// ===============================================================
//
// Given the following interface:
//
//   interface Database {
//     OpenTable(Table& table);
//   }
//
// The client would have code similar to the following:
//
//   DatabasePtr database = ...;  // Connect to database.
//   TablePtr table;
//   database->OpenTable(GetProxy(&table));
//
// Upon return from GetProxy, |table| is ready to have methods called on it.
//
// Example #2: Registering a local implementation with a remote service.
// =====================================================================
//
// Given the following interface
//   interface Collector {
//     RegisterSource(Source source);
//   }
//
// The client would have code similar to the following:
//
//   CollectorPtr collector = ...;  // Connect to Collector.
//   SourcePtr source;
//   InterfaceRequest<Source> source_request = GetProxy(&source);
//   collector->RegisterSource(std::move(source));
//   CreateSource(std::move(source_request));  // Create implementation locally.
template <typename Interface>
InterfaceRequest<Interface> GetProxy(InterfacePtr<Interface>* ptr) {
  InterfaceHandle<Interface> iface_handle;
  auto retval = GetProxy(&iface_handle);
  *ptr = InterfacePtr<Interface>::Create(std::move(iface_handle));
  return retval;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_REQUEST_H_
