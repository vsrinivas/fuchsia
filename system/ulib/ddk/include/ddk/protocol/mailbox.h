// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;
typedef struct {
    uint32_t    cmd;
    uint32_t    tx_size;
    void*       tx_buf;
} mailbox_data_buf_t;

typedef struct {
    uint32_t    mailbox;
    uint32_t    rx_size;
    void *      rx_buf;
} mailbox_channel_t;

typedef struct {
    zx_status_t (*send_cmd)(void* ctx, mailbox_channel_t* channel,
                 mailbox_data_buf_t* mdata);
} mailbox_protocol_ops_t;

typedef struct {
    mailbox_protocol_ops_t* ops;
    void* ctx;
} mailbox_protocol_t;

// sends a command via the mailbox
static inline zx_status_t mailbox_send_cmd(mailbox_protocol_t* mailbox,
                                           mailbox_channel_t* channel,
                                           mailbox_data_buf_t* mdata) {
    return mailbox->ops->send_cmd(mailbox->ctx, channel, mdata);
}
__END_CDECLS;
