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

}  // namespace internal

}  // namespace fs
