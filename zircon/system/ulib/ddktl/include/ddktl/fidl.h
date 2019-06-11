// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_FIDL_H_
#define DDK_FIDL_H_

#include <lib/fidl/llcpp/transaction.h>

class DdkTransaction : public fidl::Transaction {
public:
    DdkTransaction(fidl_txn_t* txn)
        : txn_(txn), status_called_(false) {}

    ~DdkTransaction() {
        ZX_ASSERT_MSG(status_called_, "DdkTransaction must have it's Status() method used. \
            This provides ::DdkMessage with the correct status value.\n");
    }

    /// Status() return the internal state of the DDK transaction. This MUST be called
    /// to bridge the Transaction and DDK dispatcher.
    zx_status_t Status() __WARN_UNUSED_RESULT {
      status_called_ = true;
      return reply_;
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

        reply_ = txn_->reply(txn_, &fidl_msg);
    }

    void Close(zx_status_t epitaph) final {
        ZX_ASSERT_MSG(false, "DdkTransaction does not have control of the channel lifetime.\n");
    }

    std::unique_ptr<Transaction> TakeOwnership() final {
        ZX_ASSERT_MSG(false, "DdkTransaction cannot take ownership of the transaction.\n");
    }

private:
    fidl_txn_t* txn_;
    zx_status_t reply_;
    bool status_called_;
};

#endif // DDK_FIDL_H_
