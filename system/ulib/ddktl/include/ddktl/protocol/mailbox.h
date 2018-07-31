// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/mailbox.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "mailbox-internal.h"

// DDK mailbox protocol support.
//
// :: Proxies ::
//
// ddk::MailboxProtocolProxy is a simple wrappers around mailbox_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::MailboxProtocol is a mixin class that simplifies writing DDK drivers that
// implement the mailbox protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_MAILBOX device.
// class MailboxDevice;
// using MailboxDeviceType = ddk::Device<MailboxDevice, /* ddk mixins */>;
//
// class MailboxDevice : public MailboxDeviceType,
//                   public ddk::MailboxProtocol<MailboxDevice> {
//   public:
//     MailboxDevice(zx_device_t* parent)
//       : MailboxDeviceType("my-mailbox-device", parent) {}
//
//    zx_status_t MailboxSendCmd(mailbox_channel_t* channel, mailbox_data_buf_t* mdata);
//     ...
// };

namespace ddk {

template <typename D>
class MailboxProtocol {
public:
    MailboxProtocol() {
        internal::CheckMailboxProtocolSubclass<D>();
        mailbox_proto_ops_.send_cmd = MailboxSendCmd;
    }

protected:
    mailbox_protocol_ops_t mailbox_proto_ops_ = {};

private:
    static zx_status_t MailboxSendCmd(void* ctx, mailbox_channel_t* channel,
                                      mailbox_data_buf_t* mdata) {
        return static_cast<D*>(ctx)->MailboxSendCmd(channel, mdata);
    }
};

class MailboxProtocolProxy {
public:
    MailboxProtocolProxy(mailbox_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(mailbox_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }

    zx_status_t SendCmd(mailbox_channel_t* channel, mailbox_data_buf_t* mdata) {
        return ops_->send_cmd(ctx_, channel, mdata);
    }

private:
    mailbox_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
