// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ASYNC_TRANSACTION_H_
#define LIB_FIDL_ASYNC_CPP_ASYNC_TRANSACTION_H_

#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <memory>
#include <utility>

namespace fidl {

namespace internal {

// An implementation of |fidl::Transaction|. Designed to work with |fidl::AsyncBind|, which allows
// message dispatching of multiple in-flight asynchronous transactions.
// The channel is owned by |AsyncBinding|, not the transaction.
class AsyncTransaction final : public Transaction {
 public:
  explicit AsyncTransaction(zx_txid_t txid, std::weak_ptr<AsyncBinding> binding)
      : Transaction(), txid_(txid), binding_(binding) {}

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

  void Reply(fidl::Message msg) final;

  void Close(zx_status_t epitaph) final;

  std::unique_ptr<Transaction> TakeOwnership() final;

 private:
  friend fidl::internal::AsyncBinding;

  void Dispatch(fidl_msg_t msg);

  void MoveImpl(AsyncTransaction&& other) noexcept {
    txid_ = other.txid_;
    other.txid_ = 0;
    binding_ = std::move(other.binding_);
    other.binding_ = std::weak_ptr<AsyncBinding>();
  }

  zx_txid_t txid_ = 0;
  std::weak_ptr<AsyncBinding> binding_ = {};
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ASYNC_TRANSACTION_H_
