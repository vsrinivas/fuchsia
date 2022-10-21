// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_SIMPLE_BINDING_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_SIMPLE_BINDING_H_

#include <lib/async/wait.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <utility>

namespace devfs {

class SimpleBinding;

// A basic implementation of |fidl::Transaction|. Designed to work with
// |fidl::BindSingleInFlightOnly|, which pauses message dispatching when an asynchronous transaction
// is in-flight. The channel is owned by |SimpleBinding|. |SimpleBinding| ownership ping-pongs
// between this transaction and the async dispatcher.
class ChannelTransaction final : public fidl::Transaction {
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

  zx_status_t Reply(fidl::OutgoingMessage* message, fidl::WriteOptions write_options) final;

  void Close(zx_status_t epitaph) final;

  std::unique_ptr<fidl::Transaction> TakeOwnership() final;

 private:
  friend SimpleBinding;

  void Dispatch(fidl::IncomingHeaderAndMessage& msg);

  std::unique_ptr<SimpleBinding> TakeBinding() { return std::move(binding_); }

  void MoveImpl(ChannelTransaction&& other) noexcept {
    txid_ = other.txid_;
    other.txid_ = 0;
    binding_ = std::move(other.binding_);
  }

  zx_txid_t txid_ = 0;
  std::unique_ptr<SimpleBinding> binding_ = {};
};

using AnyOnChannelClosedFn = fit::callback<void(fidl::internal::IncomingMessageDispatcher*)>;

class SimpleBinding : private async_wait_t {
 public:
  SimpleBinding(async_dispatcher_t* dispatcher, zx::channel channel,
                fidl::internal::IncomingMessageDispatcher* interface,
                AnyOnChannelClosedFn on_channel_closed_fn);

  ~SimpleBinding();

  static void MessageHandler(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                             const zx_packet_signal_t* signal);

 private:
  friend ChannelTransaction;
  friend zx_status_t BeginWait(std::unique_ptr<SimpleBinding>* unique_binding);

  zx::unowned_channel channel() const { return zx::unowned_channel(async_wait_t::object); }

  async_dispatcher_t* dispatcher_;
  fidl::internal::IncomingMessageDispatcher* interface_;
  AnyOnChannelClosedFn on_channel_closed_fn_;
};

zx_status_t BeginWait(std::unique_ptr<SimpleBinding>* unique_binding);

}  // namespace devfs

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_SIMPLE_BINDING_H_
