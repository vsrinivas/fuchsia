// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ASYNC_TRANSACTION_H_
#define LIB_FIDL_LLCPP_ASYNC_TRANSACTION_H_

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <memory>
#include <utility>

namespace fidl {

namespace internal {

// An implementation of |fidl::Transaction|. Designed to work with |fidl::BindServer|, which allows
// message dispatching of multiple in-flight asynchronous transactions from a multi-threaded
// |dispatcher|. Note that |AsyncTransaction| itself assumes that only one thread at a time will act
// on it.
// The channel is owned by |AsyncBinding|, not the transaction.
class AsyncTransaction final : public Transaction {
 public:
  explicit AsyncTransaction(zx_txid_t txid, TypeErasedServerDispatchFn dispatch_fn,
                            bool* binding_released)
      : Transaction(),
        txid_(txid),
        dispatch_fn_(dispatch_fn),
        binding_released_(binding_released) {}

  AsyncTransaction(AsyncTransaction&& other) noexcept : Transaction(std::move(other)) {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
  }

  AsyncTransaction& operator=(AsyncTransaction&& other) noexcept {
    Transaction::operator=(std::move(other));
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  virtual ~AsyncTransaction() { ZX_ASSERT(!owned_binding_); }

  zx_status_t Reply(fidl::OutgoingMessage* message) final;

  void EnableNextDispatch() final;

  void Close(zx_status_t epitaph) final;

  void InternalError(UnbindInfo error) final;

  std::unique_ptr<Transaction> TakeOwnership() final;

  bool IsUnbound() final;

 private:
  friend fidl::internal::AsyncServerBinding;

  std::optional<UnbindInfo> Dispatch(std::shared_ptr<AsyncBinding>&& binding, fidl_msg_t* msg);

  void MoveImpl(AsyncTransaction&& other) noexcept {
    txid_ = other.txid_;
    other.txid_ = 0;
    owned_binding_ = std::move(other.owned_binding_);
    unowned_binding_ = std::move(other.unowned_binding_);
    binding_released_ = other.binding_released_;
    other.binding_released_ = nullptr;
  }

  zx_txid_t txid_ = 0;
  // An AsyncTransaction may only access the AsyncBinding via one of owned_binding_ or
  // unowned_binding_. On Dispatch(), the AsyncTransaction takes ownership of the internal reference
  // used by the dispatcher (keep_alive_) in owned_binding_. Calls to EnableNextDispatch(), Close(),
  // or TakeOwnership() within the scope of Dispatch() move the reference back into keep_alive_,
  // setting unowned_binding_. Otherwise, keep_alive_ is restored from owned_binding_ prior to
  // Dispatch() returning.
  std::shared_ptr<AsyncBinding> owned_binding_ = {};
  std::weak_ptr<AsyncBinding> unowned_binding_ = {};
  TypeErasedServerDispatchFn dispatch_fn_ = {};
  bool* binding_released_ = nullptr;
  std::optional<UnbindInfo> unbind_info_;
  bool* moved_ = nullptr;
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ASYNC_TRANSACTION_H_
