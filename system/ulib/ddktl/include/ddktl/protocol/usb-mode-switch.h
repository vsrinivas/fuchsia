// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb-mode-switch.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "usb-mode-switch-internal.h"

// DDK USB mode switch protocol support.
//
// :: Proxies ::
//
// ddk::UmsProtocolProxy is a simple wrappers around usb_mode_switch_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::UmsProtocol is a mixin class that simplifies writing DDK drivers that
// implement the USB mode switch protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_USB_MODE_SWITCH device.
// class UmsDevice;
// using UmsDeviceType = ddk::Device<UmsDevice, /* ddk mixins */>;
//
// class UmsDevice : public UmsDeviceType,
//                   public ddk::UmsProtocol<UmsDevice> {
//   public:
//     UmsDevice(zx_device_t* parent)
//       : UmsDeviceType("my-usb-mode-switch-device", parent) {}
//
//        zx_status_t UmsSetMode(usb_mode_t mode);
//     ...
// };

namespace ddk {

template <typename D>
class UmsProtocol {
public:
    UmsProtocol() {
        internal::CheckUmsProtocolSubclass<D>();
        usb_mode_switch_proto_ops_.set_mode = UmsSetMode;
    }

protected:
    usb_mode_switch_protocol_ops_t usb_mode_switch_proto_ops_ = {};

private:
    static zx_status_t UmsSetMode(void* ctx, usb_mode_t mode) {
        return static_cast<D*>(ctx)->UmsSetMode(mode);
    }
};

class UmsProtocolProxy {
public:
    UmsProtocolProxy(usb_mode_switch_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t UmsSetMode(usb_mode_t mode) {
        return ops_->set_mode(ctx_, mode);
    }

private:
    usb_mode_switch_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
