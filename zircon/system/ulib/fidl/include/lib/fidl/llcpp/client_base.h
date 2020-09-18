// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_BASE_H_
#define LIB_FIDL_LLCPP_CLIENT_BASE_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

#include <fbl/intrusive_wavl_tree.h>

namespace fidl {
namespace internal {

// ResponseContext contains the state for an outstanding asynchronous transaction. It inherits from
// an intrusive container node so that ClientBase can track it. The user (or generated code) passes
// a pointer to the ResponseContext to the ClientBase, implicitly transferring ownership. Ownership
// is only returned to the caller once either OnReply() or OnError() is invoked.
// TODO(fxb/50664): fbl::WAVLTree must be made available in the SDK, otherwise it needs to be
// replaced here with some tree that is available there.
// NOTE: ResponseContext uses list_node_t in order to safely iterate over outstanding transactions
// on ClientBase destruction while invoking OnError() which can destroy the ResponseContext.
class ResponseContext : public fbl::WAVLTreeContainable<ResponseContext*>, private list_node_t {
 public:
  ResponseContext(const fidl_type_t* type, uint64_t ordinal)
      : fbl::WAVLTreeContainable<ResponseContext*>(),
        list_node_t(LIST_INITIAL_CLEARED_VALUE),
        type_(type),
        ordinal_(ordinal) {}
  virtual ~ResponseContext() = default;

  // Neither copyable nor movable.
  ResponseContext(const ResponseContext& other) = delete;
  ResponseContext& operator=(const ResponseContext& other) = delete;
  ResponseContext(ResponseContext&& other) = delete;
  ResponseContext& operator=(ResponseContext&& other) = delete;

  const fidl_type_t* type() const { return type_; }
  uint64_t ordinal() const { return ordinal_; }
  zx_txid_t Txid() const { return txid_; }

  // Invoked if a response has been received for this context.
  virtual void OnReply(uint8_t* reply) = 0;

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

  const fidl_type_t* const type_;  // Type of a response with ordinal |ordinal_|.
  const uint64_t ordinal_;         // Expected ordinal for the response.
  zx_txid_t txid_ = 0;             // Zircon txid of outstanding transaction.
};

// ChannelRefTracker takes ownership of a channel handle and is used to create and track strong
// references to it. On destruction, ChannelRefTracker can be configured to transfer ownership.
// Otherwise, the channel handle is closed.
class ChannelRefTracker {
 public:
  class ChannelRef {
   public:
    explicit ChannelRef(zx::channel channel) : handle_(channel.release()) {}
    ~ChannelRef() {
      if (on_delete_) {
        sync_completion_signal(on_delete_);
      } else {
        // If there is no waiter, the channel will be discarded, so close the handle.
        zx_handle_close(handle_);
      }
    }

    zx_handle_t handle() const { return handle_; }

    zx_handle_t SyncOnDelete(sync_completion_t* on_delete) {
      on_delete_ = on_delete;
      return handle_;
    }

   private:
    sync_completion_t* on_delete_ = nullptr;
    const zx_handle_t handle_;
  };

  // Set the given channel as the owned channel.
  void Init(zx::channel channel);

  // If the ChannelRef is still alive, returns a strong reference to it.
  std::shared_ptr<ChannelRef> Get() { return channel_weak_.lock(); }

  // Blocks on the release of any outstanding strong references to the channel and returns it. Only
  // one caller will be able to retrieve the channel. Other calls will return immediately with a
  // null channel.
  zx::channel WaitForChannel();

 private:
  std::mutex lock_;
  std::shared_ptr<ChannelRef> channel_ __TA_GUARDED(lock_);

  // Weak reference used to access channel without taking locks.
  std::weak_ptr<ChannelRef> channel_weak_;
};

// Base LLCPP client class supporting use with a multithreaded asynchronous dispatcher, safe error
// handling and unbinding, and asynchronous transaction tracking. Users should not directly interact
// with this class. ClientBase objects are expected to be managed via std::shared_ptr.
class ClientBase {
 public:
  // Creates an unbound ClientBase. Bind() must be called before any other APIs are invoked.
  ClientBase() = default;
  virtual ~ClientBase() = default;

  // Neither copyable nor movable.
  ClientBase(const ClientBase& other) = delete;
  ClientBase& operator=(const ClientBase& other) = delete;
  ClientBase(ClientBase&& other) = delete;
  ClientBase& operator=(ClientBase&& other) = delete;

  // Bind the channel to the dispatcher. Invoke on_unbound on error or unbinding.
  // NOTE: This is not thread-safe and must be called exactly once, before any other APIs.
  zx_status_t Bind(std::shared_ptr<ClientBase> client, zx::channel channel,
                   async_dispatcher_t* dispatcher, OnClientUnboundFn on_unbound);

  // Asynchronously unbind the channel from the dispatcher. on_unbound will be invoked on a
  // dispatcher thread if provided.
  void Unbind();

  // Waits for all strong references to the channel to be released, then returns it. This
  // necessarily triggers unbinding in order to release the binding's reference.
  // NOTE: As this returns a zx::channel which owns the handle, only a single call is expected to
  // succeed. Additional calls will simply return an empty zx::channel.
  zx::channel WaitForChannel();

  // Stores the given asynchronous transaction response context, setting the txid field.
  void PrepareAsyncTxn(ResponseContext* context);

  // Forget the transaction associated with the given context. Used when zx_channel_write() fails.
  void ForgetAsyncTxn(ResponseContext* context);

  // Releases all outstanding `ResponseContext`s. Invoked after the ClientBase is unbound.
  void ReleaseResponseContextsWithError();

  // Returns a strong reference to the channel to prevent destruction during a zx_channel_call() or
  // zx_channel_write(). The caller is responsible for releasing the reference.
  std::shared_ptr<ChannelRefTracker::ChannelRef> GetChannel() { return channel_tracker_.Get(); }

  // For debugging.
  size_t GetTransactionCount() {
    std::scoped_lock lock(lock_);
    return contexts_.size();
  }

  // Generated client event dispatcher function.
  virtual std::optional<UnbindInfo> DispatchEvent(fidl_msg_t* msg) = 0;

 private:
  // Dispatch function invoked by AsyncBinding on incoming message. Invokes the virtual
  // DispatchEvent().
  std::optional<UnbindInfo> Dispatch(std::shared_ptr<ClientBase>&& calling_ref, fidl_msg_t* msg);

  ChannelRefTracker channel_tracker_;

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

#endif  // LIB_FIDL_LLCPP_CLIENT_BASE_H_
