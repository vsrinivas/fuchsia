// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_BINDINGS2_INTERFACE_PTR_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include <zircon/assert.h>
#include <zx/channel.h>

#include "lib/fidl/cpp/bindings2/interface_handle.h"
#include "lib/fidl/cpp/bindings2/interface_request.h"
#include "lib/fidl/cpp/bindings2/internal/proxy_controller.h"

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
//  * |Binding|, which is the client analog of an |InterfacePtr|.
template <typename Interface>
class InterfacePtr {
 public:
  // Creates an unbound |InterfacePtr|.
  InterfacePtr() : proxy_(&controller_) {}
  InterfacePtr(std::nullptr_t) : proxy_(&controller_) {}

  InterfacePtr(const InterfacePtr& other) = delete;
  InterfacePtr& operator=(const InterfacePtr& other) = delete;

  InterfacePtr(InterfacePtr&& other)
      : controller_(std::move(other.controller_)), proxy_(&controller_) {}

  InterfacePtr& operator=(InterfacePtr&& other) {
    if (this != &other)
      controller_ = std::move(other.controller_);
    return *this;
  }

  // Bind the |InterfacePtr| to one endpoint of a newly created channel and
  // return the other endpoint as an |InterfaceRequest|.
  //
  // Typically, the returned |InterfacePtr| will be sent to a remote process to
  // be bound to an implementation of |Interface| using a |Binding| object.
  //
  // After calling this method, clients can start calling methods on this
  // |InterfacePtr|. The methods will write messages into the underlying
  // channel created by |NewRequest|, where they will be buffered by the
  // underlying channel until the |InterfaceRequest| is bound to an
  // implementation of |Interface|, potentially in a remote process.
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
  InterfaceRequest<Interface> NewRequest() {
    zx::channel h1;
    zx::channel h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK ||
        Bind(InterfaceHandle<Interface>(std::move(h1))) != ZX_OK)
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
  // Requires the current thread to have a default async_t (e.g., a message
  // loop) in order to read messages from the channel and to monitor the
  // channel for |ZX_CHANNEL_PEER_CLOSED|.
  //
  // Returns an error if the binding was not able to be created (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|).
  zx_status_t Bind(zx::channel channel) {
    return controller_.reader().Bind(std::move(channel));
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
  // Requires the current thread to have a default async_t (e.g., a message
  // loop) in order to read messages from the channel and to monitor the
  // channel for |ZX_CHANNEL_PEER_CLOSED|.
  //
  // Returns an error if the binding was not able to be created (e.g., because
  // the |channel| lacks |ZX_RIGHT_WAIT|).
  zx_status_t Bind(InterfaceHandle<Interface> handle) {
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
    return InterfaceHandle<Interface>(controller_.reader().Unbind());
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
  bool is_bound() const { return controller_.reader().is_bound(); }

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
  Interface* get() const { return &proxy_; }
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

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

  // Blocks the calling thread until either a message arrives on the previously
  // bound channel, an error occurs, or the |deadline| is exceeded.
  //
  // Returns ZX_ERR_TIMED_OUT if the deadlien is exceeded.
  //
  // Returns an error if waiting for the message, reading the message, or
  // processing the message fails. If the error results in the channel being
  // closed, the error handler will be called synchronously before this
  // method returns.
  //
  // This method can be called only if this |InterfacePtr| is currently bound to
  // a channel.
  zx_status_t WaitForResponseUntil(zx::time deadline) {
    return controller_.reader().WaitAndDispatchOneMessageUntil(deadline);
  }

  // Sets an error handler that will be called if an error causes the
  // underlying channel to be closed.
  //
  // For example, the error handler will be called if the remote side of the
  // channel sends an invalid message. When the error handler is called, the
  // |InterfacePtr| will no longer be bound to the channel.
  void set_error_handler(std::function<void()> error_handler) {
    controller_.reader().set_error_handler(std::move(error_handler));
  }

  // The underlying channel.
  const zx::channel& channel() const { return controller_.reader().channel(); }

 private:
  internal::ProxyController controller_;
  mutable typename Interface::Proxy_ proxy_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_INTERFACE_PTR_H_
