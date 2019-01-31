// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace devmgr {

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

} // namespace devmgr
