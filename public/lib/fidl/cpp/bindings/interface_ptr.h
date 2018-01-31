// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include <zircon/assert.h>
#include <zx/channel.h>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/internal/interface_ptr_internal.h"
#include "lib/fidl/cpp/bindings/macros.h"

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
// information) using Unbind(), pass it to a different thread, and
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
    Unbind();
    internal_state_.Swap(&other.internal_state_);
    return *this;
  }

  // Closes the bound channel (if any) on destruction.
  ~InterfacePtr() {}

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
    ZX_DEBUG_ASSERT(!is_bound());

    zx::channel endpoint0;
    zx::channel endpoint1;
    zx::channel::create(0, &endpoint0, &endpoint1);
    Bind(InterfaceHandle<Interface>(std::move(endpoint0)));
    return InterfaceRequest<Interface>(std::move(endpoint1));
  }

  // Binds the InterfacePtr to a remote implementation of Interface.
  //
  // Calling with an invalid |info| (containing an invalid channel handle)
  // has the same effect as reset(). In this case, the InterfacePtr is not
  // considered as bound.
  void Bind(InterfaceHandle<Interface> handle) {
    Unbind();
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

  // Tests as true if bound, false if not.
  explicit operator bool() const { return internal_state_.is_bound(); }

  // Blocks the current thread until the next incoming response callback arrives
  // or an error occurs. Returns |true| if a response arrived, or |false| in
  // case of error.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // channel.
  bool WaitForResponse() {
    return WaitForResponseUntil(zx::time::infinite());
  }

  // Blocks the current thread until the next incoming response callback
  // arrives, an error occurs, or the deadline is exceeded. Returns |true| if a
  // response arrived, or |false| otherwise. Use |encountered_error| to know
  // if an error occurred, of if the timeout exceeded.
  //
  // This method may only be called after the InterfacePtr has been bound to a
  // channel.
  bool WaitForResponseUntil(zx::time deadline) {
    return internal_state_.WaitForResponseUntil(deadline);
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
  void set_error_handler(std::function<void()> error_handler) {
    internal_state_.set_error_handler(std::move(error_handler));
  }

  // Unbinds the InterfacePtr and returns the information which could be used
  // to setup an InterfacePtr again. This method may be used to move the proxy
  // to a different thread (see class comments for details).
  InterfaceHandle<Interface> Unbind() {
    State state;
    internal_state_.Swap(&state);
    return state.Unbind();
  }

  // DO NOT USE. Exposed only for internal use and for testing.
  internal::InterfacePtrState<Interface>* internal_state() {
    return &internal_state_;
  }

  // The underlying channel.
  const zx::channel& channel() const { return internal_state_.channel(); }

 private:
  typedef internal::InterfacePtrState<Interface> State;
  mutable State internal_state_;

  FIDL_MOVE_ONLY_TYPE(InterfacePtr);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_H_
