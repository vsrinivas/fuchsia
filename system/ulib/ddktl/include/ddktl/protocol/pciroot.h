// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pciroot.fidl INSTEAD.

#pragma once

#include <ddk/protocol/pciroot.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "pciroot-internal.h"

// DDK pciroot-protocol support
//
// :: Proxies ::
//
// ddk::PcirootProtocolProxy is a simple wrapper around
// pciroot_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::PcirootProtocol is a mixin class that simplifies writing DDK drivers
// that implement the pciroot protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PCIROOT device.
// class PcirootDevice {
// using PcirootDeviceType = ddk::Device<PcirootDevice, /* ddk mixins */>;
//
// class PcirootDevice : public PcirootDeviceType,
//                       public ddk::PcirootProtocol<PcirootDevice> {
//   public:
//     PcirootDevice(zx_device_t* parent)
//         : PcirootDeviceType("my-pciroot-protocol-device", parent) {}
//
//     zx_status_t PcirootGetAuxdata(const char* args, void* out_data_buffer, size_t data_size,
//     size_t* out_data_actual);
//
//     zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx_handle_t* out_bti);
//
//     ...
// };

namespace ddk {

template <typename D>
class PcirootProtocol : public internal::base_mixin {
public:
    PcirootProtocol() {
        internal::CheckPcirootProtocolSubclass<D>();
        pciroot_protocol_ops_.get_auxdata = PcirootGetAuxdata;
        pciroot_protocol_ops_.get_bti = PcirootGetBti;
    }

protected:
    pciroot_protocol_ops_t pciroot_protocol_ops_ = {};

private:
    static zx_status_t PcirootGetAuxdata(void* ctx, const char* args, void* out_data_buffer,
                                         size_t data_size, size_t* out_data_actual) {
        return static_cast<D*>(ctx)->PcirootGetAuxdata(args, out_data_buffer, data_size,
                                                       out_data_actual);
    }
    static zx_status_t PcirootGetBti(void* ctx, uint32_t bdf, uint32_t index,
                                     zx_handle_t* out_bti) {
        return static_cast<D*>(ctx)->PcirootGetBti(bdf, index, out_bti);
    }
};

class PcirootProtocolProxy {
public:
    PcirootProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    PcirootProtocolProxy(const pciroot_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(pciroot_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetAuxdata(const char* args, void* out_data_buffer, size_t data_size,
                           size_t* out_data_actual) {
        return ops_->get_auxdata(ctx_, args, out_data_buffer, data_size, out_data_actual);
    }
    zx_status_t GetBti(uint32_t bdf, uint32_t index, zx_handle_t* out_bti) {
        return ops_->get_bti(ctx_, bdf, index, out_bti);
    }

private:
    pciroot_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
