// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mailbox_send_cmd, MailboxSendCmd,
        zx_status_t (C::*)(mailbox_channel_t*, mailbox_data_buf_t*));

template <typename D>
constexpr void CheckMailboxProtocolSubclass() {
    static_assert(internal::has_mailbox_send_cmd<D>::value,
                  "MailboxProtocol subclasses must implement "
                  "MailboxSendCmd(mailbox_channel_t* channel, mailbox_data_buf_t* mdata)");
 }

}  // namespace internal
}  // namespace ddk
