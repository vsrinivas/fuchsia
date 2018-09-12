// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bt_hci.fidl INSTEAD.

#pragma once

#include <ddk/protocol/bt-hci.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "bt-hci-internal.h"

// DDK bt-hci-protocol support
//
// :: Proxies ::
//
// ddk::BtHciProtocolProxy is a simple wrapper around
// bt_hci_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::BtHciProtocol is a mixin class that simplifies writing DDK drivers
// that implement the bt-hci protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BT_HCI device.
// class BtHciDevice {
// using BtHciDeviceType = ddk::Device<BtHciDevice, /* ddk mixins */>;
//
// class BtHciDevice : public BtHciDeviceType,
//                     public ddk::BtHciProtocol<BtHciDevice> {
//   public:
//     BtHciDevice(zx_device_t* parent)
//         : BtHciDeviceType("my-bt-hci-protocol-device", parent) {}
//
//     zx_status_t BtHciOpenCommandChannel(zx_handle_t* out_channel);
//
//     zx_status_t BtHciOpenAclDataChannel(zx_handle_t* out_channel);
//
//     zx_status_t BtHciOpenSnoopChannel(zx_handle_t* out_channel);
//
//     ...
// };

namespace ddk {

template <typename D>
class BtHciProtocol : public internal::base_protocol {
public:
    BtHciProtocol() {
        internal::CheckBtHciProtocolSubclass<D>();
        ops_.open_command_channel = BtHciOpenCommandChannel;
        ops_.open_acl_data_channel = BtHciOpenAclDataChannel;
        ops_.open_snoop_channel = BtHciOpenSnoopChannel;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_BT_HCI;
        ddk_proto_ops_ = &ops_;
    }

protected:
    bt_hci_protocol_ops_t ops_ = {};

private:
    // Open the two-way HCI command channel for sending HCI commands and
    // receiving event packets.  Returns ZX_ERR_ALREADY_BOUND if the channel
    // is already open.
    static zx_status_t BtHciOpenCommandChannel(void* ctx, zx_handle_t* out_channel) {
        return static_cast<D*>(ctx)->BtHciOpenCommandChannel(out_channel);
    }
    // Open the two-way HCI ACL data channel.
    // Returns ZX_ERR_ALREADY_BOUND if the channel is already open.
    static zx_status_t BtHciOpenAclDataChannel(void* ctx, zx_handle_t* out_channel) {
        return static_cast<D*>(ctx)->BtHciOpenAclDataChannel(out_channel);
    }
    // Open an output-only channel for monitoring HCI traffic.
    // The format of each message is: [1-octet flags] [n-octet payload]
    // The flags octet is a bitfield with the following values defined:
    //  - 0x00: The payload represents a command packet sent from the host to the
    //          controller.
    //  - 0x01: The payload represents an event packet sent by the controller.
    // Returns ZX_ERR_ALREADY_BOUND if the channel is already open.
    static zx_status_t BtHciOpenSnoopChannel(void* ctx, zx_handle_t* out_channel) {
        return static_cast<D*>(ctx)->BtHciOpenSnoopChannel(out_channel);
    }
};

class BtHciProtocolProxy {
public:
    BtHciProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    BtHciProtocolProxy(const bt_hci_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(bt_hci_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Open the two-way HCI command channel for sending HCI commands and
    // receiving event packets.  Returns ZX_ERR_ALREADY_BOUND if the channel
    // is already open.
    zx_status_t OpenCommandChannel(zx_handle_t* out_channel) {
        return ops_->open_command_channel(ctx_, out_channel);
    }
    // Open the two-way HCI ACL data channel.
    // Returns ZX_ERR_ALREADY_BOUND if the channel is already open.
    zx_status_t OpenAclDataChannel(zx_handle_t* out_channel) {
        return ops_->open_acl_data_channel(ctx_, out_channel);
    }
    // Open an output-only channel for monitoring HCI traffic.
    // The format of each message is: [1-octet flags] [n-octet payload]
    // The flags octet is a bitfield with the following values defined:
    //  - 0x00: The payload represents a command packet sent from the host to the
    //          controller.
    //  - 0x01: The payload represents an event packet sent by the controller.
    // Returns ZX_ERR_ALREADY_BOUND if the channel is already open.
    zx_status_t OpenSnoopChannel(zx_handle_t* out_channel) {
        return ops_->open_snoop_channel(ctx_, out_channel);
    }

private:
    bt_hci_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
