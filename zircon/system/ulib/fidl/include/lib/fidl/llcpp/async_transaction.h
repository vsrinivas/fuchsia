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

// An implementation of |fidl::Transaction|. Designed to work with
// |fidl::BindServer|, which allows message dispatching of multiple in-flight
// asynchronous transactions from a multi-threaded async dispatcher. Note that
// |SyncTransaction| itself is only thread-compatible.
//
// This transaction must always be constructed on the stack and used
// synchronously by the server method handler. As such, its implementation is
// optimized for synchronous use.
class SyncTransaction final : public Transaction {
 public:
  SyncTransaction(zx_txid_t txid, AsyncServerBinding* binding, bool* next_wait_begun_early)
      : txid_(txid), binding_(binding), next_wait_begun_early_(next_wait_begun_early) {}

  SyncTransaction(SyncTransaction&& other) noexcept = delete;
  SyncTransaction& operator=(SyncTransaction&& other) noexcept = delete;

  std::optional<DispatchError> Dispatch(fidl::IncomingMessage&& msg);

  zx_status_t Reply(fidl::OutgoingMessage* message) final;

  void EnableNextDispatch() final;

  void Close(zx_status_t epitaph) final;

  void InternalError(UnbindInfo error, ErrorOrigin origin) final;

  std::unique_ptr<Transaction> TakeOwnership() final;

  bool IsUnbound() final;

 private:
  friend class AsyncTransaction;

  zx_txid_t txid_ = 0;
  AsyncServerBinding* binding_ = nullptr;
  bool* next_wait_begun_early_ = nullptr;
  std::optional<DispatchError> error_;

  std::shared_ptr<AsyncServerBinding> binding_lifetime_extender_ = {};
};

// An implementation of |fidl::Transaction|. Designed to work with
// |fidl::BindServer|, which allows message dispatching of multiple in-flight
// asynchronous transactions from a multi-threaded async dispatcher. Note that
// |AsyncTransaction| itself is only thread-compatible.
//
// This transaction must always be constructed on the heap and used
// asynchronously by the server method handler (via an asynchronous completer).
// As such, its implementation is specialized to allow binding teardown to
// happen at any point in the background.
class AsyncTransaction final : public Transaction {
 public:
  explicit AsyncTransaction(SyncTransaction&& transaction)
      : txid_(transaction.txid_), binding_(transaction.binding_->shared_from_this()) {}

  AsyncTransaction(AsyncTransaction&& other) noexcept = default;
  AsyncTransaction& operator=(AsyncTransaction&& other) noexcept = default;

  zx_status_t Reply(fidl::OutgoingMessage* message) final;

  void EnableNextDispatch() final;

  void Close(zx_status_t epitaph) final;

  void InternalError(UnbindInfo error, ErrorOrigin origin) final;

  std::unique_ptr<Transaction> TakeOwnership() final;

  bool IsUnbound() final;

 private:
  zx_txid_t txid_ = 0;
  std::weak_ptr<AsyncServerBinding> binding_ = {};
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ASYNC_TRANSACTION_H_
