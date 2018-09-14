// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_hda_codec.fidl INSTEAD.

#pragma once

#include <ddk/protocol/intel-hda-codec.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "intel-hda-codec-internal.h"

// DDK ihda-codec-protocol support
//
// :: Proxies ::
//
// ddk::IhdaCodecProtocolProxy is a simple wrapper around
// ihda_codec_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::IhdaCodecProtocol is a mixin class that simplifies writing DDK drivers
// that implement the ihda-codec protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_IHDA_CODEC device.
// class IhdaCodecDevice {
// using IhdaCodecDeviceType = ddk::Device<IhdaCodecDevice, /* ddk mixins */>;
//
// class IhdaCodecDevice : public IhdaCodecDeviceType,
//                         public ddk::IhdaCodecProtocol<IhdaCodecDevice> {
//   public:
//     IhdaCodecDevice(zx_device_t* parent)
//         : IhdaCodecDeviceType("my-ihda-codec-protocol-device", parent) {}
//
//     zx_status_t IhdaCodecGetDriverChannel(zx_handle_t* out_channel);
//
//     ...
// };

namespace ddk {

template <typename D>
class IhdaCodecProtocol : public internal::base_mixin {
public:
    IhdaCodecProtocol() {
        internal::CheckIhdaCodecProtocolSubclass<D>();
        ihda_codec_protocol_ops_.get_driver_channel = IhdaCodecGetDriverChannel;
    }

protected:
    ihda_codec_protocol_ops_t ihda_codec_protocol_ops_ = {};

private:
    // Fetch a zx_handle_t to a channel which can be used to communicate with the codec device.
    static zx_status_t IhdaCodecGetDriverChannel(void* ctx, zx_handle_t* out_channel) {
        return static_cast<D*>(ctx)->IhdaCodecGetDriverChannel(out_channel);
    }
};

class IhdaCodecProtocolProxy {
public:
    IhdaCodecProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    IhdaCodecProtocolProxy(const ihda_codec_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(ihda_codec_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Fetch a zx_handle_t to a channel which can be used to communicate with the codec device.
    zx_status_t GetDriverChannel(zx_handle_t* out_channel) {
        return ops_->get_driver_channel(ctx_, out_channel);
    }

private:
    ihda_codec_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
