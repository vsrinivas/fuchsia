// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FIDL_TXN_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FIDL_TXN_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

// Manages state of a FIDL transaction for the DevMgr so we can reply to the correct message.
// DevmgrFidlTxn must not outlive the channel it is given
class DevmgrFidlTxn : public fidl::Transaction {
 public:
  DevmgrFidlTxn(zx::unowned_channel channel, uint32_t txid)
      : channel_(channel), txid_(txid), status_called_(false) {}
  DevmgrFidlTxn(const zx::channel& channel, uint32_t txid)
      : channel_(channel), txid_(txid), status_called_(false) {}

  DevmgrFidlTxn& operator=(const DevmgrFidlTxn&) = delete;
  DevmgrFidlTxn(const DevmgrFidlTxn&) = delete;

  DevmgrFidlTxn& operator=(DevmgrFidlTxn&&) = delete;
  DevmgrFidlTxn(DevmgrFidlTxn&&) = delete;

  ~DevmgrFidlTxn() {
    ZX_ASSERT_MSG(status_called_,
                  "DevmgrFidlTxn must have it's Status() method used. \
          This provides Devmgr with the correct status value.\n");
  }

  void Reply(fidl::Message message) {
    ZX_ASSERT_MSG(txid_, "DevmgrFidlTxn must have its transaction id set.\n");
    const fidl_msg_t msg{
        .bytes = message.bytes().data(),
        .handles = message.handles().data(),
        .num_bytes = static_cast<uint32_t>(message.bytes().size()),
        .num_handles = static_cast<uint32_t>(message.handles().size()),
    };

    auto hdr = static_cast<fidl_message_header_t*>(msg.bytes);
    hdr->txid = txid_;
    status_ = channel_->write(0, msg.bytes, msg.num_bytes, msg.handles, msg.num_handles);

    // We have now transferred ownership of the message's handles over the channel.
    message.ClearHandlesUnsafe();
  }

  void Close(zx_status_t close_status) final {
    // no-op for devmgr
  }

  std::unique_ptr<Transaction> TakeOwnership() final {
    ZX_ASSERT_MSG(false, "DevmgrFidlTxn cannot take ownership of the transaction.\n");
  }

  zx_status_t Status() __WARN_UNUSED_RESULT {
    status_called_ = true;
    return status_;
  }

 private:
  // Reply channel
  const zx::unowned_channel channel_;

  // Transaction id of the message we're replying to
  const uint32_t txid_;

  // Has the Status method been called?
  bool status_called_;

  // Status is OK by default since not all functions call Reply
  zx_status_t status_ = ZX_OK;
};

// Manages state of a FIDL transaction so we can reply to the correct message.
// FidlTxn must not outlive the channel it is given
class FidlTxn {
 public:
  FidlTxn(zx::unowned_channel channel, uint32_t txid) : channel_(channel), txid_(txid) {}
  FidlTxn(const zx::channel& channel, uint32_t txid) : channel_(channel), txid_(txid) {}

  FidlTxn& operator=(const FidlTxn&) = delete;
  FidlTxn(const FidlTxn&) = delete;

  FidlTxn& operator=(FidlTxn&&) = delete;
  FidlTxn(FidlTxn&&) = delete;

  zx_status_t Reply(const fidl_msg_t* msg) {
    auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);
    hdr->txid = txid_;
    return channel_->write(0, msg->bytes, msg->num_bytes, msg->handles, msg->num_handles);
  }

  static zx_status_t FidlReply(fidl_txn_t* reply, const fidl_msg_t* msg) {
    static_assert(offsetof(FidlTxn, txn_) == 0);
    return reinterpret_cast<FidlTxn*>(reply)->Reply(msg);
  }

  fidl_txn_t* fidl_txn() { return &txn_; }

 private:
  // Due to the implementation of FidlReply, it is important that this be the
  // first member variable.
  fidl_txn_t txn_ = {.reply = FidlTxn::FidlReply};

  // Reply channel
  const zx::unowned_channel channel_;

  // Transaction id of the message we're replying to
  const uint32_t txid_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FIDL_TXN_H_
