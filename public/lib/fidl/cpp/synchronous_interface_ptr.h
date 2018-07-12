// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_SYNCHRONOUS_INTERFACE_PTR_H_
#define LIB_FIDL_CPP_SYNCHRONOUS_INTERFACE_PTR_H_

#include <stddef.h>

#include <memory>
#include <utility>

#include "lib/fidl/cpp/interface_handle.h"

namespace fidl {

// A syclient interface to a remote implementation of |Interface|.
//
// An |SynchronousInterfacePtr| implements |Interface| by proxying calls through
// a |channel| to a remote implementation of |Interface|. Method calls on the
// |Interface| proxy are encoded and sent through the bound channel to the
// remote endpoint, which processes them. If the method has a reply (including
// any empty reply), the client blocks and waits for the remote endpoint to
// reply.
//
// You need to bind the |SynchronousInterfacePtr| before calling any |Interface|
// methods. There are a number of ways to bind the |SynchronousInterfacePtr|.
// See |NewRequest|, |Bind|, and the |BindSync| method on |InterfaceHandle|.
//
// This class is thread-compatible. Once bound, the |SynchronousInterfacePtr|
// can be used from multiple threads simultaneously. However, the
// |SynchronousInterfacePtr| does not attempt to synchronize mutating operatios,
// such as |Bind| or |Unbind|.
//
// |SynchronousInterfacePtr| does not require a |async_t| implementation and
// does not bind to the default |async_dispatcher_t*| for the current thread, unlike
// |InterfacePtr|.
//
// See also:
//
//  * |Binding|, which is the server analog of an |SynchronousInterfacePtr|.
//  * |InterfacePtr|, which is an asynchronous interface to a remote
//    implementation.
template <typename Interface>
class SynchronousInterfacePtr {
 public:
  using InterfaceSync = typename Interface::Sync_;

  // Creates an unbound |SynchronousInterfacePtr|.
  SynchronousInterfacePtr() {}
  SynchronousInterfacePtr(std::nullptr_t) {}

  SynchronousInterfacePtr(const SynchronousInterfacePtr& other) = delete;
  SynchronousInterfacePtr& operator=(const SynchronousInterfacePtr& other) =
      delete;

  SynchronousInterfacePtr(SynchronousInterfacePtr&& other) = default;
  SynchronousInterfacePtr& operator=(SynchronousInterfacePtr&& other) = default;

  // Bind the |SynchronousInterfacePtr| to one endpoint of a newly created
  // channel and return the other endpoint as an |InterfaceRequest|.
  //
  // Typically, the returned |InterfaceRequest| will be sent to a remote process
  // to be bound to an implementation of |Interface| using a |Binding| object.
  //
  // After calling this method, clients can start calling methods on this
  // |SynchronousInterfacePtr|. However, methods that have replies will block
  // until the remote implementation binds the |InterfaceRequest| and replies.
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
  //   TableSyncPtr table;
  //   database->OpenTable(table.NewRequest());
  //
  // The client can call methods on |table| immediately. Messages that have
  // replies will block until the Database implementation binds a Table
  // implementation and replies.
  InterfaceRequest<Interface> NewRequest() {
    zx::channel h1;
    zx::channel h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK)
      return nullptr;
    Bind(std::move(h1));
    return InterfaceRequest<Interface>(std::move(h2));
  }

  // Binds the |SynchronousInterfacePtr| to the given |channel|.
  //
  // The |SynchronousInterfacePtr| expects the remote end of the |channel| to
  // speak the protocol defined by |Interface|. Unlike the |Bind| overload that
  // takes a |InterfaceHandle| parameter, this |Bind| overload lacks type
  // safety.
  //
  // If the |SynchronousInterfacePtr| was prevously bound to another channel,
  // that channel is closed. If the |channel| is invalid, then this method will
  // effectively unbind the |SynchronousInterfacePtr|. A more direct way to have
  // that effect is to call |Unbind|.
  //
  // Does not require the current thread to have a default async_t.
  void Bind(zx::channel channel) {
    if (!channel) {
      proxy_.reset();
      return;
    }
    proxy_.reset(new typename InterfaceSync::Proxy_(std::move(channel)));
  }

  // Binds the |SynchronousInterfacePtr| to the given |InterfaceHandle|.
  //
  // The |SynchronousInterfacePtr| expects the remote end of the |channel| to
  // speak the protocol defined by |Interface|. Unlike the |Bind| overload that
  // takes a |channel| parameter, this |Bind| overload provides type safety.
  //
  // If the |SynchronousInterfacePtr| was prevously bound to another channel,
  // that channel is closed. If the |InterfaceHandle| is invalid, then this
  // method will effectively unbind the |SynchronousInterfacePtr|. A more direct
  // way to have that effect is to call |Unbind|.
  //
  // Does not require the current thread to have a default async_t.
  void Bind(InterfaceHandle<Interface> handle) {
    return Bind(handle.TakeChannel());
  }

  // Unbinds the underlying channel from the |SynchronousInterfacePtr|.
  //
  // The underlying channel is returned as an |InterfaceHandle|, which is safe
  // to transport to another thread or process.
  //
  // After this method returns, a subsequent call to |Bind| is required before
  // calling any additional |Interface| methods.
  InterfaceHandle<Interface> Unbind() {
    InterfaceHandle<Interface> handle(proxy_->proxy().TakeChannel());
    proxy_.reset();
    return handle;
  }

  // Whether this |SynchronousInterfacePtr| is currently bound to a channel.
  //
  // If the |SynchronousInterfacePtr| is bound to a channel, calls to
  // |Interface| methods are proxied to the remote endpoint of the channel.
  //
  // See also:
  //
  //  * |Bind|, which binds a channel to this |SynchronousInterfacePtr|.
  //  * |Unbind|, which unbinds a channel from this |SynchronousInterfacePtr|.
  bool is_bound() const { return static_cast<bool>(proxy_); }

  // Whether this |SynchronousInterfacePtr| is currently bound to a channel.
  //
  // See |is_bound| for details.
  explicit operator bool() const { return is_bound(); }

  // The |Interface| proxy associated with this |SynchronousInterfacePtr|.
  //
  // When this |SynchronousInterfacePtr| is bound, method calls on this
  // |Interface| will be proxied to the remote endpoint of the connection.
  // Methods that expect replies will block until the
  // |SynchronousInterfacePtr| either receives a reply to that transaction.
  //
  // When this |SynchronousInterfacePtr| is not bound, this method returns
  // nullptr.
  //
  // The returned |Interface| is thread-compatible and can be used from any
  // thread.
  InterfaceSync* get() const { return proxy_.get(); }
  InterfaceSync* operator->() const { return get(); }
  InterfaceSync& operator*() const { return *get(); }

 private:
  std::unique_ptr<typename InterfaceSync::Proxy_> proxy_;
};

// Temporary structure to help migrate the return type for synchronous methods
// from bool to zx_status_t. Unlike zx_status_t, this type does not implicitly
// convert to boo.
struct Sync2Status {
  explicit Sync2Status(zx_status_t s) : statvs(s) {}

  // Note the creative spelling of status as statvs to make it easier to grep
  // for clients of this scaffolding and remove them.
  const zx_status_t statvs;
};

template <typename Interface>
class Synchronous2InterfacePtr {
 public:
  using InterfaceSync = typename Interface::Sync2_;

  Synchronous2InterfacePtr() {}
  Synchronous2InterfacePtr(std::nullptr_t) {}

  Synchronous2InterfacePtr(const Synchronous2InterfacePtr& other) = delete;
  Synchronous2InterfacePtr& operator=(const Synchronous2InterfacePtr& other) =
      delete;

  Synchronous2InterfacePtr(Synchronous2InterfacePtr&& other) = default;
  Synchronous2InterfacePtr& operator=(Synchronous2InterfacePtr&& other) =
      default;

  InterfaceRequest<Interface> NewRequest() {
    zx::channel h1;
    zx::channel h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK)
      return nullptr;
    Bind(std::move(h1));
    return InterfaceRequest<Interface>(std::move(h2));
  }

  void Bind(zx::channel channel) {
    if (!channel) {
      proxy_.reset();
      return;
    }
    proxy_.reset(new typename InterfaceSync::Proxy_(std::move(channel)));
  }

  void Bind(InterfaceHandle<Interface> handle) {
    return Bind(handle.TakeChannel());
  }

  InterfaceHandle<Interface> Unbind() {
    InterfaceHandle<Interface> handle(proxy_->proxy().TakeChannel());
    proxy_.reset();
    return handle;
  }

  bool is_bound() const { return static_cast<bool>(proxy_); }
  explicit operator bool() const { return is_bound(); }
  InterfaceSync* get() const { return proxy_.get(); }
  InterfaceSync* operator->() const { return get(); }
  InterfaceSync& operator*() const { return *get(); }

 private:
  std::unique_ptr<typename InterfaceSync::Proxy_> proxy_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_SYNCHRONOUS_INTERFACE_PTR_H_
