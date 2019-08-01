// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_FIDL_H_
#define DDK_FIDL_H_

#include <lib/fidl/llcpp/transaction.h>

class DdkTransaction : public fidl::Transaction {
 public:
  DdkTransaction(fidl_txn_t* txn) : txn_(txn) {}

  ~DdkTransaction() {
    ZX_ASSERT_MSG(status_called_,
                  "DdkTransaction must have it's Status() method used. \
            This provides ::DdkMessage with the correct status value.\n");
  }

  /// Status() return the internal state of the DDK transaction. This MUST be called
  /// to bridge the Transaction and DDK dispatcher.
  zx_status_t Status() __WARN_UNUSED_RESULT {
    status_called_ = true;
    return status_;
  }

 protected:
  void Reply(fidl::Message msg) final {
    ZX_ASSERT(txn_);

    const fidl_msg_t fidl_msg{
        .bytes = msg.bytes().data(),
        .handles = msg.handles().data(),
        .num_bytes = static_cast<uint32_t>(msg.bytes().size()),
        .num_handles = static_cast<uint32_t>(msg.handles().size()),
    };

    status_ = txn_->reply(txn_, &fidl_msg);
    msg.ClearHandlesUnsafe();
  }

  void Close(zx_status_t close_status) final { status_ = close_status; }

  std::unique_ptr<Transaction> TakeOwnership() final {
    ZX_ASSERT_MSG(false, "DdkTransaction cannot take ownership of the transaction.\n");
  }

 private:
  fidl_txn_t* txn_;
  zx_status_t status_ = ZX_OK;
  bool status_called_ = false;
};

#endif  // DDK_FIDL_H_
