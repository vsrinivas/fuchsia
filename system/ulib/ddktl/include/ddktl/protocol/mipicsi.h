// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mipicsi.banjo INSTEAD.

#pragma once

#include <ddk/protocol/mipicsi.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "mipicsi-internal.h"

// DDK mipi-csi-protocol support
//
// :: Proxies ::
//
// ddk::MipiCsiProtocolProxy is a simple wrapper around
// mipi_csi_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::MipiCsiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the mipi-csi protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_MIPI_CSI device.
// class MipiCsiDevice {
// using MipiCsiDeviceType = ddk::Device<MipiCsiDevice, /* ddk mixins */>;
//
// class MipiCsiDevice : public MipiCsiDeviceType,
//                       public ddk::MipiCsiProtocol<MipiCsiDevice> {
//   public:
//     MipiCsiDevice(zx_device_t* parent)
//         : MipiCsiDeviceType("my-mipi-csi-protocol-device", parent) {}
//
//     zx_status_t MipiCsiInit(const mipi_info_t* mipi_info, const mipi_adap_info_t* adap_info);
//
//     zx_status_t MipiCsiDeInit();
//
//     ...
// };

namespace ddk {

template <typename D>
class MipiCsiProtocol : public internal::base_protocol {
public:
    MipiCsiProtocol() {
        internal::CheckMipiCsiProtocolSubclass<D>();
        ops_.init = MipiCsiInit;
        ops_.de_init = MipiCsiDeInit;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_MIPI_CSI;
        ddk_proto_ops_ = &ops_;
    }

protected:
    mipi_csi_protocol_ops_t ops_ = {};

private:
    static zx_status_t MipiCsiInit(void* ctx, const mipi_info_t* mipi_info,
                                   const mipi_adap_info_t* adap_info) {
        return static_cast<D*>(ctx)->MipiCsiInit(mipi_info, adap_info);
    }
    static zx_status_t MipiCsiDeInit(void* ctx) { return static_cast<D*>(ctx)->MipiCsiDeInit(); }
};

class MipiCsiProtocolProxy {
public:
    MipiCsiProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    MipiCsiProtocolProxy(const mipi_csi_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(mipi_csi_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t Init(const mipi_info_t* mipi_info, const mipi_adap_info_t* adap_info) {
        return ops_->init(ctx_, mipi_info, adap_info);
    }
    zx_status_t DeInit() { return ops_->de_init(ctx_); }

private:
    mipi_csi_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
