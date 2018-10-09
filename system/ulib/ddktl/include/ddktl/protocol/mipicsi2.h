// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mipicsi2 INSTEAD.

#pragma once

#include <ddk/protocol/mipicsi2.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "mipicsi2-internal.h"

// DDK mipi-csi2protocol support
//
// :: Proxies ::
//
// ddk::MipiCsi2ProtocolProxy is a simple wrapper around
// mipi_csi2protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::MipiCsi2Protocol is a mixin class that simplifies writing DDK drivers
// that implement the mipi-csi2 protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_MIPI_CSI2 device.
// class MipiCsi2Device {
// using MipiCsi2DeviceType = ddk::Device<MipiCsi2Device, /* ddk mixins */>;
//
// class MipiCsi2Device : public MipiCsi2DeviceType,
//                        public ddk::MipiCsi2Protocol<MipiCsi2Device> {
//   public:
//     MipiCsi2Device(zx_device_t* parent)
//         : MipiCsi2DeviceType("my-mipi-csi2protocol-device", parent) {}
//
//     zx_status_t MipiCsi2Init(const mipi_info_t* info);
//
//     zx_status_t MipiCsi2DeInit();
//
//     ...
// };

namespace ddk {

template <typename D>
class MipiCsi2Protocol : public internal::base_protocol {
public:
    MipiCsi2Protocol() {
        internal::CheckMipiCsi2ProtocolSubclass<D>();
        ops_.init = MipiCsi2Init;
        ops_.de_init = MipiCsi2DeInit;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_MIPI_CSI2;
        ddk_proto_ops_ = &ops_;
    }

protected:
    mipi_csi2protocol_ops_t ops_ = {};

private:
    static zx_status_t MipiCsi2Init(void* ctx, const mipi_info_t* info) {
        return static_cast<D*>(ctx)->MipiCsi2Init(info);
    }
    static zx_status_t MipiCsi2DeInit(void* ctx) { return static_cast<D*>(ctx)->MipiCsi2DeInit(); }
};

class MipiCsi2ProtocolProxy {
public:
    MipiCsi2ProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    MipiCsi2ProtocolProxy(const mipi_csi2protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(mipi_csi2protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t Init(const mipi_info_t* info) { return ops_->init(ctx_, info); }
    zx_status_t DeInit() { return ops_->de_init(ctx_); }

private:
    mipi_csi2protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
