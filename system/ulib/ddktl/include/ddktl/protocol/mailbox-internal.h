// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mailbox.banjo INSTEAD.

#pragma once

#include <ddk/protocol/mailbox.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mailbox_protocol_send_command, MailboxSendCommand,
                                     zx_status_t (C::*)(const mailbox_channel_t* channel,
                                                        const mailbox_data_buf_t* mdata));

template <typename D>
constexpr void CheckMailboxProtocolSubclass() {
    static_assert(internal::has_mailbox_protocol_send_command<D>::value,
                  "MailboxProtocol subclasses must implement "
                  "zx_status_t MailboxSendCommand(const mailbox_channel_t* channel, const "
                  "mailbox_data_buf_t* mdata");
}

} // namespace internal
} // namespace ddk
