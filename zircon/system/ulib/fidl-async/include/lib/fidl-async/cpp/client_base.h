// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_CLIENT_BASE_H_
#define LIB_FIDL_ASYNC_CPP_CLIENT_BASE_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/async_bind_internal.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

namespace fidl {
namespace internal {

// Struct used to track an outstanding asynchronous transaction.
struct ResponseContext {
  // Intrusive list node for tracking this ResponseContext.
  // TODO(madhaviyengar): Replace this once an intrusive tree/map is added to the SDK.
  list_node_t node = LIST_INITIAL_CLEARED_VALUE;
  zx_txid_t txid = 0;  // txid of outstanding transaction.
};

// Base LLCPP client class supporting use with a multithreaded asynchronous dispatcher, safe error
// handling and unbinding, and asynchronous transaction tracking. Users of generated client classes
// derived from `ClientBase` should only be aware of the public APIs.
class ClientBase {
 public:
  // If the binding is not already unbound or in the process of being unbound, unbinds the channel
  // from the dispatcher, invoking on_unbound if provided. Note that this object will have been
  // destroyed prior to on_unbound being invoked.
  virtual ~ClientBase();

  // Asynchronously unbind the channel from the dispatcher. on_unbound will be invoked on a
  // dispatcher thread if provided.
  void Unbind();

 protected:
  // Transfer ownership of the channel to a new client, initializing state.
  ClientBase(zx::channel channel, async_dispatcher_t* dispatcher,
             TypeErasedOnUnboundFn on_unbound);

  // Bind the channel to the dispatcher. Invoke on_unbound on error or unbinding.
  zx_status_t Bind();

  // Stores the given asynchronous transaction response context, setting the txid field.
  void PrepareAsyncTxn(ResponseContext* context);

  // Forget the transaction associated with the given context. Used when zx_channel_write() fails.
  void ForgetAsyncTxn(ResponseContext* context);

  // Returns a strong reference to binding to prevent channel deletion during a zx_channel_call() or
  // zx_channel_write(). The caller is responsible for releasing the reference.
  std::shared_ptr<AsyncBinding> GetBinding();

  // Invoked by InternalDispatch() below. If `context` is non-null, the message was the response to
  // an asynchronous transaction. Otherwise, the message was an event.
  virtual void Dispatch(fidl_msg_t* msg, ResponseContext* context) = 0;

  // Dispatch function invoked by AsyncBinding on incoming message. The client only requires `msg`.
  void InternalDispatch(std::shared_ptr<AsyncBinding>&, fidl_msg_t* msg, bool*,
                        zx_status_t* status);

  // Weak reference to the internal binding state.
  std::weak_ptr<AsyncBinding> binding_;

  // State for tracking outstanding transactions.
  std::mutex lock_;
  // The base node of an intrusive container of ResponseContexts corresponding to outstanding
  // asynchronous transactions.
  ResponseContext __TA_GUARDED(lock_) contexts_ = {};
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_CLIENT_BASE_H_
