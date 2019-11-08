// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_DEVHOST_FIDL_H_
#define SRC_DEVICES_HOST_DEVHOST_FIDL_H_

#include <lib/fidl/llcpp/transaction.h>

class DevhostTransaction : public fidl::Transaction {
 public:
  explicit DevhostTransaction(fidl_txn_t* txn) : txn_(fs::FidlConnection::CopyTxn(txn)) {}

  ~DevhostTransaction() {
    ZX_ASSERT_MSG(status_called_,
                  "DevhostTransaction must have it's Status() method used. \
            This provides ::DevhostMessage with the correct status value.\n");
  }

  /// Status() return the internal state of the DDK transaction. This MUST be called
  /// to bridge the Transaction and DDK dispatcher.
  zx_status_t Status() __WARN_UNUSED_RESULT {
    status_called_ = true;
    return status_;
  }

 protected:
  void Reply(fidl::Message msg) final {
    const fidl_msg_t fidl_msg{
        .bytes = msg.bytes().data(),
        .handles = msg.handles().data(),
        .num_bytes = static_cast<uint32_t>(msg.bytes().size()),
        .num_handles = static_cast<uint32_t>(msg.handles().size()),
    };

    status_ = txn_.Txn()->reply(txn_.Txn(), &fidl_msg);
    msg.ClearHandlesUnsafe();
  }

  void Close(zx_status_t close_status) final { status_ = close_status; }

  std::unique_ptr<Transaction> TakeOwnership() final {
    // Can't check this for Async transactions
    // This function is only valid for devhost. We know that we aren't closing the handle of the
    // channel so that it doesn't get a BAD_HANDLE until after this transaction is destroyed
    status_called_ = true;
    return std::make_unique<DevhostTransaction>(*this);
  }

 private:
  fs::FidlConnection txn_;
  zx_status_t status_ = ZX_OK;
  bool status_called_ = false;
};

#endif  // SRC_DEVICES_HOST_DEVHOST_FIDL_H_
