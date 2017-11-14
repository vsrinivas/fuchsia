// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include <zx/channel.h>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/internal/interface_ptr_internal.h"
#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace fidl {

template <typename Interface>
class InterfaceRequest;

// A pointer to a local proxy of a remote Interface implementation. Uses a
// channel to communicate with the remote implementation, and automatically
// closes the channel and deletes the proxy on destruction. The pointer must be
// bound to a channel before the interface methods can be called.
//
// This class is thread hostile, as is the local proxy it manages. All calls to
// this class or the proxy should be from the same thread that created it. If
// you need to move the proxy to a different thread, extract the
// InterfaceHandle (containing just the channel and any version
// information) using PassInterfaceHandle(), pass it to a different thread, and
// create and bind a new InterfacePtr from that thread.
template <typename Interface>
class InterfacePtr {
 public:
  // Constructs an unbound InterfacePtr.
  InterfacePtr() {}
  InterfacePtr(std::nullptr_t) {}

  // Takes over the binding of another InterfacePtr.
  InterfacePtr(InterfacePtr&& other) {
    internal_state_.Swap(&other.internal_state_);
  }

  // Takes over the binding of another InterfacePtr, and closes any channel
  // already bound to this pointer.
  InterfacePtr& operator=(InterfacePtr&& other) {
    reset();
    internal_state_.Swap(&other.internal_state_);
    return *this;
  }

  // Assigning nullptr to this class causes it to close the currently bound
  // channel (if any) and returns the pointer to the unbound state.
  InterfacePtr& operator=(std::nullptr_t) {
    reset();
    return *this;
  }

  // Closes the bound channel (if any) on destruction.
  ~InterfacePtr() {}

  // If |info| is valid (containing a valid channel handle), returns an
  // InterfacePtr bound to it. Otherwise, returns an unbound InterfacePtr.
  static InterfacePtr<Interface> Create(InterfaceHandle<Interface> info) {
    InterfacePtr<Interface> ptr;
    if (info.is_valid())
      ptr.Bind(std::move(info));
    return ptr;
  }

  // Creates a new pair of channels, one end bound to this InterfacePtr<>, and
  // returns the other end as a InterfaceRequest<>. InterfaceRequest<> should
  // be passed to whatever will provide the implementation.
  //
  // Example.  Given the following interface:
  //
  //   interface Database {
  //     OpenTable(Table& table);
  //   }
  //
  // The client would have code similar to the following:
  //
  //   DatabasePtr database = ...;  // Connect to database.
  //   TablePtr table;
  //   database->OpenTable(table.NewRequest());
  //
  // Upon return from .NewRequest(), |table| is ready to have methods called
  // on it.
  InterfaceRequest<Interface> NewRequest() {
    FXL_DCHECK(!is_bound()) << "An existing handle is already bound.";

    zx::channel endpoint0;
    zx::channel endpoint1;
    zx::channel::create(0, &endpoint0, &endpoint1);
    Bind(InterfaceHandle<Interface>(std::move(endpoint0), Interface::Version_));
    return InterfaceRequest<Interface>(std::move(endpoint1));
  }

  // Binds the InterfacePtr to a remote implementation of Interface.
  //
  // Calling with an invalid |info| (containing an invalid channel handle)
  // has the same effect as reset(). In this case, the InterfacePtr is not
  // considered as bound.
  void Bind(InterfaceHandle<Interface> handle) {
    reset();
    if (handle.is_valid())
      internal_state_.Bind(std::move(handle));
  }

  // Returns whether or not this InterfacePtr is bound to a channel.
  bool is_bound() const { return internal_state_.is_bound(); }

  // Returns a raw pointer to the local proxy. Caller does not take ownership.
  // Note that the local proxy is thread hostile, as stated above.
  Interface* get() const { return internal_state_.instance(); }

  // Functions like a pointer to Interface. Must already be bound.
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

  // Returns the version number of the interface that the remote side supports.
  uint32_t version() const { return internal_state_.version(); }

  // If the remote side doesn't support the specified version, it will close its
  // end of the channel asynchronously. This does nothing if it's already
  // known that the remote side supports the specified version, i.e., if
  // |version <= this->version()|.
  //
  // After calling RequireVersion() with a version not supported by the remote
  // side, all subsequent calls to interface methods will be ignored.
  void RequireVersion(uint32_t version) {
    internal_state_.RequireVersion(version);
  }

  // Closes the bound channel (if any) and returns the pointer to the
  // unbound state.
  void reset() {
    State doomed;
    internal_state_.Swap(&doomed);
  }

  // Tests as true if bound, false if not.
  explicit operator bool() const { return internal_state_.is_bound(); }

  // Blocks the current thread until the next incoming response callback arrives
  // or an error occurs. Returns |true| if a response arrived, or |false| in
  // case of error.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // channel.
  bool WaitForIncomingResponse() {
    return internal_state_.WaitForIncomingResponse(fxl::TimeDelta::Max());
  }

  // Blocks the current thread until the next incoming response callback
  // arrives, an error occurs, or the timeout exceeded. Returns |true| if a
  // response arrived, or |false| otherwise. Use |encountered_error| to know
  // if an error occurred, of if the timeout exceeded.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // channel.
  bool WaitForIncomingResponseWithTimeout(fxl::TimeDelta timeout) {
    return internal_state_.WaitForIncomingResponse(timeout);
  }

  // Indicates whether the channel has encountered an error. If true,
  // method calls made on this interface will be dropped (and may already have
  // been dropped).
  bool encountered_error() const { return internal_state_.encountered_error(); }

  // Registers a handler to receive error notifications. The handler will be
  // called from the thread that owns this InterfacePtr.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // channel.
  void set_connection_error_handler(fxl::Closure error_handler) {
    internal_state_.set_connection_error_handler(std::move(error_handler));
  }

  // Unbinds the InterfacePtr and returns the information which could be used
  // to setup an InterfacePtr again. This method may be used to move the proxy
  // to a different thread (see class comments for details).
  InterfaceHandle<Interface> PassInterfaceHandle() {
    State state;
    internal_state_.Swap(&state);

    return state.PassInterfaceHandle();
  }

  // DO NOT USE. Exposed only for internal use and for testing.
  internal::InterfacePtrState<Interface>* internal_state() {
    return &internal_state_;
  }

 private:
  typedef internal::InterfacePtrState<Interface> State;
  mutable State internal_state_;

  FIDL_MOVE_ONLY_TYPE(InterfacePtr);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_
