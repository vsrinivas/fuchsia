// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_INTERFACE_PTR_H_

#include <algorithm>
#include <cstddef>
#include <utility>

#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"

namespace fidl {

// A client interface to a remote implementation of |Interface|.
//
// An |InterfacePtr| implements |Interface| by proxying calls through a
// |channel| to a remote implementation of |Interface|. Method calls on the
// |Interface| proxy are encoded and sent through the bound channel to the
// remote endpoint, which processes them. If the remote endpoint has not yet
// been bound to an implementation, messages sent on the channel are buffered
// by the channel, allowing for *pipelined* operation.
//
// The |InterfacePtr| also keeps state about the connection and about
// outstanding request transactions that are expecting replies. When a the
// |InterfacePtr| receives a reply to an outstanding transaction, the
// |InterfacePtr| decodes the reply and calls the appropriate callback on the
// thread on which the |InterfacePtr| was bound.
//
// You need to bind the |InterfacePtr| before calling any |Interface| methods.
// There are a number of ways to bind the |InterfacePtr|.  See |NewRequest|,
// |Bind|, and the |Bind| method on |InterfaceHandle|.
//
// If the underlying channel experiences an error, the |InterfacePtr| will
// unbind from the channel and call its error handler.
//
// This class is thread-hostile, as is the local proxy it manages. All calls to
// this class or the proxy should be from the thread on which the
// |InterfacePtr| was bound. If you need to move the proxy to a different
// thread, extract the |InterfaceHandle| by calling |Unbind|, and pass the
// |InterfaceHandle| to a different thread, which the |InterfaceHandle| can be
// bound to an |InterfacePtr| again. This operation destroys the state about
// outstanding request transactions that are expecting replies.
//
// See also:
//
//  * |Binding|, which is the server analog of an |InterfacePtr|.
//  * |SynchronousInterfacePtr|, which is a synchronous client interface to a
//    remote implementation.
template <typename Interface>
class InterfacePtr {
 public:
  using Proxy = typename Interface::Proxy_;

  // Creates an unbound |InterfacePtr|.
  InterfacePtr() : impl_(new Impl) {}
  InterfacePtr(std::nullptr_t) : InterfacePtr() {}

  InterfacePtr(const InterfacePtr& other) = delete;
  InterfacePtr& operator=(const InterfacePtr& other) = delete;

  InterfacePtr(InterfacePtr&& other) : impl_(std::move(other.impl_)) {
    other.impl_.reset(new Impl);
  }

  InterfacePtr& operator=(InterfacePtr&& other) {
    if (this != &other) {
      impl_ = std::move(other.impl_);
      other.impl_.reset(new Impl);
    }
    return *this;
  }

  // Bind the |InterfacePtr| to one endpoint of a newly created channel and
  // return the other endpoint as an |InterfaceRequest|.
  //
  // Typically, the returned |InterfaceRequest| will be sent to a remote process
  // to be bound to an implementation of |Interface| using a |Binding| object.
  //
  // After calling this method, clients can start calling methods on this
  // |InterfacePtr|. The methods will write messages into the underlying
  // channel created by |NewRequest|, where they will be buffered by the
  // underlying channel until the |InterfaceRequest| is bound to an
  // implementation of |Interface|, potentially in a remote process.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  //
  // # Example
  //
  // Given the following interface:
  //
  //   interface Database {
  //     OpenTable(request<Table> table);
  //   };
  //
  // The client can use the |NewRequest| method to create the |InterfaceRequest|
  // object needed by the |OpenTable| method:
  //
  //   DatabasePtr database = ...;  // Connect to database.
  //   TablePtr table;
  //   database->OpenTable(table.NewRequest());
  //
  // The client can call methods on |table| immediately.
  InterfaceRequest<Interface> NewRequest(
      async_dispatcher_t* dispatcher = nullptr) {
    zx::channel h1;
    zx::channel h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK ||
        Bind(std::move(h1), dispatcher) != ZX_OK)
      return nullptr;
    return InterfaceRequest<Interface>(std::move(h2));
  }

  // Binds the |InterfacePtr| to the given |channel|.
  //
  // The |InterfacePtr| expects the remote end of the |channel| to speak the
  // protocol defined by |Interface|. Unlike the |Bind| overload that takes a
  // |InterfaceHandle| parameter, this |Bind| overload lacks type safety.
  //
  // If the |InterfacePtr| was prevously bound to another channel, that channel
  // is closed. If the |channel| is invalid, then this method will effectively
  // unbind the |InterfacePtr|. A more direct way to have that effect is to call
  // |Unbind|.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  //
  // Returns an error if the binding was not able to be created (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|).
  zx_status_t Bind(zx::channel channel,
                   async_dispatcher_t* dispatcher = nullptr) {
    return impl_->controller.reader().Bind(std::move(channel), dispatcher);
  }

  // Binds the |InterfacePtr| to the given |InterfaceHandle|.
  //
  // The |InterfacePtr| expects the remote end of the |channel| to speak the
  // protocol defined by |Interface|. Unlike the |Bind| overload that takes a
  // |channel| parameter, this |Bind| overload provides type safety.
  //
  // If the |InterfacePtr| was prevously bound to another channel, that channel
  // is closed. If the |InterfaceHandle| is invalid, then this method will
  // effectively unbind the |InterfacePtr|. A more direct way to have that
  // effect is to call |Unbind|.
  //
  // Uses the given async_t (e.g., a message loop) in order to read messages
  // from the channel and to monitor the channel for |ZX_CHANNEL_PEER_CLOSED|.
  // If |dispatcher| is null, the current thread must have a default async_t.
  //
  // Returns an error if the binding was not able to be created (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|).
  zx_status_t Bind(InterfaceHandle<Interface> handle,
                   async_dispatcher_t* dispatcher = nullptr) {
    return Bind(handle.TakeChannel());
  }

  // Unbinds the underlying channel from the |InterfacePtr|.
  //
  // The underlying channel is returned as an |InterfaceHandle|, which is safe
  // to transport to another thread or process. Any callbacks waiting for
  // replies from the remote endpoint are discarded and any outstanding
  // transaction state is erased.
  //
  // After this method returns, a subsequent call to |Bind| is required before
  // calling any additional |Interface| methods.
  InterfaceHandle<Interface> Unbind() {
    return InterfaceHandle<Interface>(impl_->controller.reader().Unbind());
  }

  // Whether this |InterfacePtr| is currently bound to a channel.
  //
  // If the |InterfacePtr| is bound to a channel, the |InterfacePtr| has
  // affinity for the thread on which it was bound and calls to |Interface|
  // methods are proxied to the remote endpoint of the channel.
  //
  // See also:
  //
  //  * |Bind|, which binds a channel to this |InterfacePtr|.
  //  * |Unbind|, which unbinds a channel from this |InterfacePtr|.
  bool is_bound() const { return impl_->controller.reader().is_bound(); }

  // Whether this |InterfacePtr| is currently bound to a channel.
  //
  // See |is_bound| for details.
  explicit operator bool() const { return is_bound(); }

  // The |Interface| proxy associated with this |InterfacePtr|.
  //
  // When this |InterfacePtr| is bound, method calls on this |Interface| will
  // be proxied to the remote endpoint of the connection. Methods that expect
  // replies will retain the supplied callbacks until the |InterfacePtr| either
  // receives a reply to that transaction or the |InterfacePtr| is unbound from
  // the channel.
  //
  // When this |InterfacePtr| is not bound, it is an error to call methods on
  // the returned |Interface|.
  //
  // The returned |Interface| is thread-hostile and can be used only on the
  // thread on which the |InterfacePtr| was bound.
  Interface* get() const { return &impl_->proxy; }
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

  // An object on which to register for FIDL events.
  //
  // Arriving events are dispatched to the callbacks stored on this object.
  // Events for unbound callbacks are ignored.
  Proxy& events() const { return impl_->proxy; }

#ifdef FIDL_ENABLE_LEGACY_WAIT_FOR_RESPONSE

  // DEPRECATED: Using InterfaceSyncPtr instead. If used in a test, consider
  // spinning the async::Loop instead.
  //
  // Blocks the calling thread until either a message arrives on the previously
  // bound channel or an error occurs.
  //
  // Returns an error if waiting for the message, reading the message, or
  // processing the message fails. If the error results in the channel being
  // closed, the error handler will be called synchronously before this
  // method returns.
  //
  // This method can be called only if this |InterfacePtr| is currently bound to
  // a channel.
  zx_status_t WaitForResponse() {
    return WaitForResponseUntil(zx::time::infinite());
  }

  // DEPRECATED: Using InterfaceSyncPtr instead. If used in a test, consider
  // spinning the async::Loop instead.
  //
  // Blocks the calling thread until either a message arrives on the previously
  // bound channel, an error occurs, or the |deadline| is exceeded.
  //
  // Returns ZX_ERR_TIMED_OUT if the deadline is exceeded.
  //
  // Returns an error if waiting for the message, reading the message, or
  // processing the message fails. If the error results in the channel being
  // closed, the error handler will be called synchronously before this
  // method returns.
  //
  // This method can be called only if this |InterfacePtr| is currently bound to
  // a channel.
  zx_status_t WaitForResponseUntil(zx::time deadline) {
    return impl_->controller.reader().WaitAndDispatchOneMessageUntil(deadline);
  }
#endif

  // Sets an error handler that will be called if an error causes the
  // underlying channel to be closed.
  //
  // For example, the error handler will be called if the remote side of the
  // channel sends an invalid message. When the error handler is called, the
  // |InterfacePtr| will no longer be bound to the channel.
  void set_error_handler(fit::closure error_handler) {
    impl_->controller.reader().set_error_handler(std::move(error_handler));
  }

  // The underlying channel.
  const zx::channel& channel() const {
    return impl_->controller.reader().channel();
  }

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

template <typename T>
struct InterfacePtr<T>::Impl {
  Impl() : proxy(&controller) { controller.set_proxy(&proxy); }
  internal::ProxyController controller;
  mutable Proxy proxy;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERFACE_PTR_H_
