// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/limits.h>
#include <lib/fidl/llcpp/transaction.h>
#include <stdint.h>

#include <type_traits>

#include <fbl/function.h>
#include <fs/internal/connection.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

namespace fs {

namespace internal {

class FidlTransaction final : public ::fidl::Transaction {
 public:
  FidlTransaction(zx_txid_t transaction_id, std::weak_ptr<internal::Binding> binding)
      : transaction_id_(transaction_id), binding_(std::move(binding)) {}

  ~FidlTransaction() final;

  FidlTransaction(FidlTransaction&& other) noexcept : Transaction(std::move(other)) {
    MoveImpl(std::move(other));
  }

  FidlTransaction& operator=(FidlTransaction&& other) noexcept {
    if (this != &other) {
      Transaction::operator=(std::move(other));
      MoveImpl(std::move(other));
    }
    return *this;
  }

  void Reply(fidl::Message msg) final;

  void Close(zx_status_t epitaph) final;

  std::unique_ptr<Transaction> TakeOwnership() final;

  zx_status_t status() const;

  enum class Result {
    kRepliedSynchronously,
    kPendingAsyncReply,
    kClosed
  };

  // Destructively convert the transaction into the result of handling a FIDL method.
  Result ToResult();

 private:
  void MoveImpl(FidlTransaction&& other) noexcept {
    transaction_id_ = other.transaction_id_;
    other.transaction_id_ = 0;
    binding_ = std::move(other.binding_);
    status_ = other.status_;
    other.status_ = ZX_OK;
  }

  zx_txid_t transaction_id_ = 0;
  std::weak_ptr<Binding> binding_;
  zx_status_t status_ = ZX_OK;
};

// A helper class exposing a C binding |fidl_txn_t| interface in front of an LLCPP transaction.
class CTransactionShim : public fidl_txn_t {
 public:
  explicit CTransactionShim(FidlTransaction* transaction)
      : fidl_txn_t{&CTransactionShim::Reply}, transaction_(transaction) {}

  // Propagate any error returned from the C bindings to the LLCPP transaction.
  void PropagateError(zx_status_t status);

 private:
  static zx_status_t Reply(fidl_txn_t* txn, const fidl_msg_t* msg);

  FidlTransaction* transaction_;
};

}  // namespace internal

}  // namespace fs
