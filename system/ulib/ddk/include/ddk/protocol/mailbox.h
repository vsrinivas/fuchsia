// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mailbox.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct mailbox_data_buf mailbox_data_buf_t;
typedef struct mailbox_channel mailbox_channel_t;
typedef struct mailbox_protocol mailbox_protocol_t;

// Declarations

struct mailbox_data_buf {
    uint32_t cmd;
    void* tx_buffer;
    size_t tx_size;
};

struct mailbox_channel {
    uint32_t mailbox;
    void* rx_buffer;
    size_t rx_size;
};

typedef struct mailbox_protocol_ops {
    zx_status_t (*send_command)(void* ctx, const mailbox_channel_t* channel,
                                const mailbox_data_buf_t* mdata);
} mailbox_protocol_ops_t;

struct mailbox_protocol {
    mailbox_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t mailbox_send_command(const mailbox_protocol_t* proto,
                                               const mailbox_channel_t* channel,
                                               const mailbox_data_buf_t* mdata) {
    return proto->ops->send_command(proto->ctx, channel, mdata);
}

__END_CDECLS;
