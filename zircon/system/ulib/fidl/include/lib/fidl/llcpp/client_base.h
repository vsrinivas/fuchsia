// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_CLIENT_BASE_H_
#define LIB_FIDL_ASYNC_CPP_CLIENT_BASE_H_

#include <fbl/intrusive_wavl_tree.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

namespace fidl {
namespace internal {

// ResponseContext contains the state for an outstanding asynchronous transaction. It inherits from
// an intrusive container node so that ClientBase can track it.
// TODO(fxb/50664): fbl::WAVLTree must be made available in the SDK, otherwise it needs to be
// replaced here with some tree that is available there.
// NOTE: ResponseContext uses list_node_t in order to safely iterate over outstanding transactions
// on ClientBase destruction while invoking OnError() which can destroy the ResponseContext.
class ResponseContext : public fbl::WAVLTreeContainable<ResponseContext*>, private list_node_t {
 public:
  ResponseContext()
      : fbl::WAVLTreeContainable<ResponseContext*>(),
        list_node_t(LIST_INITIAL_CLEARED_VALUE) {}
  virtual ~ResponseContext() = default;

  // Neither copyable nor movable.
  ResponseContext(const ResponseContext& other) = delete;
  ResponseContext& operator=(const ResponseContext& other) = delete;
  ResponseContext(ResponseContext&& other) = delete;
  ResponseContext& operator=(ResponseContext&& other) = delete;

  zx_txid_t Txid() const { return txid_; }

  // Invoked if an error occurs handling the response message prior to invoking the user-specified
  // callback or if the ClientBase is destroyed with the transaction outstanding. Note that
  // OnError() may be invoked within ~ClientBase(), so the user must ensure that a ClientBase
  // is not destroyed while holding any locks OnError() would take.
  virtual void OnError() = 0;

 private:
  friend class ClientBase;

  // For use with fbl::WAVLTree.
  struct Traits {
    static zx_txid_t GetKey(const ResponseContext& context) { return context.txid_; }
    static bool LessThan(const zx_txid_t& key1, const zx_txid_t& key2) { return key1 < key2; }
    static bool EqualTo(const zx_txid_t& key1, const zx_txid_t& key2) { return key1 == key2; }
  };

  zx_txid_t txid_ = 0;  // Zircon txid of outstanding transaction.
};

// Base LLCPP client class supporting use with a multithreaded asynchronous dispatcher, safe error
// handling and unbinding, and asynchronous transaction tracking. Users should not directly interact
// with this class.
class ClientBase {
 public:
  // Transfer ownership of the channel to a new client, initializing state.
  ClientBase(zx::channel channel, async_dispatcher_t* dispatcher, TypeErasedOnUnboundFn on_unbound);

  // If the binding is not already unbound or in the process of being unbound, unbinds the channel
  // from the dispatcher, invoking on_unbound if provided. Note that this object will have been
  // destroyed prior to on_unbound being invoked.
  virtual ~ClientBase();

  // Neither copyable nor movable.
  ClientBase(const ClientBase& other) = delete;
  ClientBase& operator=(const ClientBase& other) = delete;
  ClientBase(ClientBase&& other) = delete;
  ClientBase& operator=(ClientBase&& other) = delete;

  // Bind the channel to the dispatcher. Invoke on_unbound on error or unbinding.
  zx_status_t Bind();

  // Asynchronously unbind the channel from the dispatcher. on_unbound will be invoked on a
  // dispatcher thread if provided.
  void Unbind();

  // Must only be called from the message handler on a dispatcher thread having received an epitaph
  // from the server. Like Unbind(), except that the epitaph is forwarded to on_unbound.
  void Close(zx_status_t epitaph);

  // Stores the given asynchronous transaction response context, setting the txid field.
  void PrepareAsyncTxn(ResponseContext* context);

  // Forget the transaction associated with the given context. Used when zx_channel_write() fails.
  void ForgetAsyncTxn(ResponseContext* context);

  // Returns a strong reference to binding to prevent channel deletion during a zx_channel_call() or
  // zx_channel_write(). The caller is responsible for releasing the reference.
  std::shared_ptr<AsyncBinding> GetBinding() { return binding_.lock(); }

  // For debugging.
  size_t GetTransactionCount() {
    std::scoped_lock lock(lock_);
    return contexts_.size();
  }

  // Generated client dispatch function. If the ResponseContext* is non-null, the message is a
  // response to an asynchronous transaction. Otherwise, it is an event.
  virtual zx_status_t Dispatch(fidl_msg_t* msg, ResponseContext* context) = 0;

 private:
  // Dispatch function invoked by AsyncBinding on incoming message. Invokes the virtual Dispatch().
  zx_status_t Dispatch(fidl_msg_t* msg);

  // Weak reference to the internal binding state.
  std::weak_ptr<AsyncBinding> binding_;

  // State for tracking outstanding transactions.
  std::mutex lock_;
  // The base node of an intrusive container of ResponseContexts corresponding to outstanding
  // asynchronous transactions.
  fbl::WAVLTree<zx_txid_t, ResponseContext*, ResponseContext::Traits> contexts_ __TA_GUARDED(lock_);
  // Mirror list used to safely invoke OnError() on outstanding ResponseContexts in ~ClientBase().
  list_node_t delete_list_ __TA_GUARDED(lock_) = LIST_INITIAL_VALUE(delete_list_);
  zx_txid_t txid_base_ __TA_GUARDED(lock_) = 0;  // Value used to compute the next txid.
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_CLIENT_BASE_H_
