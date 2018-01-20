// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    // Open the two-way HCI command channel for sending HCI commands and
    // receiving event packets.  Returns ZX_ERR_ALREADY_BOUND if the channel
    // is already open.
    zx_status_t (*open_command_channel)(void* ctx, zx_handle_t* out_channel);

    // Open the two-way HCI ACL data channel.
    // Returns ZX_ERR_ALREADY_BOUND if the channel is already open.
    zx_status_t (*open_acl_data_channel)(void* ctx, zx_handle_t* out_channel);

    // Open an output-only channel for monitoring HCI traffic.
    // The format of each message is: [1-octet flags] [n-octet payload]
    // The flags octet is a bitfield with the following values defined:
    //  - 0x00: The payload represents a command packet sent from the host to the
    //          controller.
    //  - 0x01: The payload represents an event packet sent by the controller.
    // Returns ZX_ERR_ALREADY_BOUND if the channel is already open.
    zx_status_t (*open_snoop_channel)(void* ctx, zx_handle_t* out_channel);
} bt_hci_protocol_ops_t;

typedef struct {
    bt_hci_protocol_ops_t* ops;
    void* ctx;
} bt_hci_protocol_t;

static inline zx_status_t bt_hci_open_command_channel(bt_hci_protocol_t* bt_hci,
                                                      zx_handle_t* out_channel) {
    return bt_hci->ops->open_command_channel(bt_hci->ctx, out_channel);
}

static inline zx_status_t bt_hci_open_acl_data_channel(bt_hci_protocol_t* bt_hci,
                                                       zx_handle_t* out_channel) {
    return bt_hci->ops->open_acl_data_channel(bt_hci->ctx, out_channel);
}

static inline zx_status_t bt_hci_open_snoop_channel(bt_hci_protocol_t* bt_hci,
                                                    zx_handle_t* out_channel) {
    return bt_hci->ops->open_snoop_channel(bt_hci->ctx, out_channel);
}

__END_CDECLS;
