// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_CHANNEL_TRANSACTION_H_
#define LIB_FIDL_ASYNC_CPP_CHANNEL_TRANSACTION_H_

#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <utility>

namespace fidl {

namespace internal {

class SimpleBinding;

// A basic implementation of |fidl::Transaction|. Designed to work with
// |fidl::BindSingleInFlightOnly|, which pauses message dispatching when an asynchronous transaction
// is in-flight. The channel is owned by |SimpleBinding|. |SimpleBinding| ownership ping-pongs
// between this transaction and the async dispatcher.
class ChannelTransaction final : public Transaction {
 public:
  ChannelTransaction(zx_txid_t txid, std::unique_ptr<SimpleBinding> binding)
      : Transaction(), txid_(txid), binding_(std::move(binding)) {}

  ~ChannelTransaction() final;

  ChannelTransaction(ChannelTransaction&& other) noexcept : Transaction(std::move(other)) {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
  }

  ChannelTransaction& operator=(ChannelTransaction&& other) noexcept {
    Transaction::operator=(std::move(other));
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  zx_status_t Reply(fidl::OutgoingMessage* message) final;

  void Close(zx_status_t epitaph) final;

  std::unique_ptr<Transaction> TakeOwnership() final;

 private:
  friend fidl::internal::SimpleBinding;

  void Dispatch(fidl_msg_t msg);

  std::unique_ptr<SimpleBinding> TakeBinding() { return std::move(binding_); }

  void MoveImpl(ChannelTransaction&& other) noexcept {
    txid_ = other.txid_;
    other.txid_ = 0;
    binding_ = std::move(other.binding_);
  }

  zx_txid_t txid_ = 0;
  std::unique_ptr<SimpleBinding> binding_ = {};
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_CHANNEL_TRANSACTION_H_
