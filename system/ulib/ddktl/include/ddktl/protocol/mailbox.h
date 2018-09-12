// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mailbox.banjo INSTEAD.

#pragma once

#include <ddk/protocol/mailbox.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "mailbox-internal.h"

// DDK mailbox-protocol support
//
// :: Proxies ::
//
// ddk::MailboxProtocolProxy is a simple wrapper around
// mailbox_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::MailboxProtocol is a mixin class that simplifies writing DDK drivers
// that implement the mailbox protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_MAILBOX device.
// class MailboxDevice {
// using MailboxDeviceType = ddk::Device<MailboxDevice, /* ddk mixins */>;
//
// class MailboxDevice : public MailboxDeviceType,
//                       public ddk::MailboxProtocol<MailboxDevice> {
//   public:
//     MailboxDevice(zx_device_t* parent)
//         : MailboxDeviceType("my-mailbox-protocol-device", parent) {}
//
//     zx_status_t MailboxSendCommand(const mailbox_channel_t* channel, const mailbox_data_buf_t*
//     mdata);
//
//     ...
// };

namespace ddk {

template <typename D>
class MailboxProtocol : public internal::base_mixin {
public:
    MailboxProtocol() {
        internal::CheckMailboxProtocolSubclass<D>();
        mailbox_protocol_ops_.send_command = MailboxSendCommand;
    }

protected:
    mailbox_protocol_ops_t mailbox_protocol_ops_ = {};

private:
    static zx_status_t MailboxSendCommand(void* ctx, const mailbox_channel_t* channel,
                                          const mailbox_data_buf_t* mdata) {
        return static_cast<D*>(ctx)->MailboxSendCommand(channel, mdata);
    }
};

class MailboxProtocolProxy {
public:
    MailboxProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    MailboxProtocolProxy(const mailbox_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(mailbox_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t SendCommand(const mailbox_channel_t* channel, const mailbox_data_buf_t* mdata) {
        return ops_->send_command(ctx_, channel, mdata);
    }

private:
    mailbox_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
