// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_hda_dsp.fidl INSTEAD.

#pragma once

#include <ddk/protocol/intel-hda-dsp.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "intel-hda-dsp-internal.h"

// DDK ihda-dsp-protocol support
//
// :: Proxies ::
//
// ddk::IhdaDspProtocolProxy is a simple wrapper around
// ihda_dsp_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::IhdaDspProtocol is a mixin class that simplifies writing DDK drivers
// that implement the ihda-dsp protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_IHDA_DSP device.
// class IhdaDspDevice {
// using IhdaDspDeviceType = ddk::Device<IhdaDspDevice, /* ddk mixins */>;
//
// class IhdaDspDevice : public IhdaDspDeviceType,
//                       public ddk::IhdaDspProtocol<IhdaDspDevice> {
//   public:
//     IhdaDspDevice(zx_device_t* parent)
//         : IhdaDspDeviceType("my-ihda-dsp-protocol-device", parent) {}
//
//     void IhdaDspGetDevInfo(zx_pcie_device_info_t* out_out);
//
//     zx_status_t IhdaDspGetMmio(zx_handle_t* out_vmo, size_t* out_size);
//
//     zx_status_t IhdaDspGetBti(zx_handle_t* out_bti);
//
//     void IhdaDspEnable();
//
//     void IhdaDspDisable();
//
//     zx_status_t IhdaDspIrqEnable(const ihda_dsp_irq_t* callback);
//
//     void IhdaDspIrqDisable();
//
//     ...
// };

namespace ddk {

template <typename D>
class IhdaDspProtocol : public internal::base_mixin {
public:
    IhdaDspProtocol() {
        internal::CheckIhdaDspProtocolSubclass<D>();
        ihda_dsp_protocol_ops_.get_dev_info = IhdaDspGetDevInfo;
        ihda_dsp_protocol_ops_.get_mmio = IhdaDspGetMmio;
        ihda_dsp_protocol_ops_.get_bti = IhdaDspGetBti;
        ihda_dsp_protocol_ops_.enable = IhdaDspEnable;
        ihda_dsp_protocol_ops_.disable = IhdaDspDisable;
        ihda_dsp_protocol_ops_.irq_enable = IhdaDspIrqEnable;
        ihda_dsp_protocol_ops_.irq_disable = IhdaDspIrqDisable;
    }

protected:
    ihda_dsp_protocol_ops_t ihda_dsp_protocol_ops_ = {};

private:
    // Fetch the parent HDA controller's PCI device info.
    static void IhdaDspGetDevInfo(void* ctx, zx_pcie_device_info_t* out_out) {
        static_cast<D*>(ctx)->IhdaDspGetDevInfo(out_out);
    }
    // Fetch a VMO that represents the BAR holding the Audio DSP registers.
    static zx_status_t IhdaDspGetMmio(void* ctx, zx_handle_t* out_vmo, size_t* out_size) {
        return static_cast<D*>(ctx)->IhdaDspGetMmio(out_vmo, out_size);
    }
    // Fetch a handle to our bus transaction initiator.
    static zx_status_t IhdaDspGetBti(void* ctx, zx_handle_t* out_bti) {
        return static_cast<D*>(ctx)->IhdaDspGetBti(out_bti);
    }
    // Enables DSP
    static void IhdaDspEnable(void* ctx) { static_cast<D*>(ctx)->IhdaDspEnable(); }
    // Disable DSP
    static void IhdaDspDisable(void* ctx) { static_cast<D*>(ctx)->IhdaDspDisable(); }
    // Enables DSP interrupts and set a callback to be invoked when an interrupt is
    // raised.
    // Returns `ZX_ERR_ALREADY_EXISTS` if a callback is already set.
    static zx_status_t IhdaDspIrqEnable(void* ctx, const ihda_dsp_irq_t* callback) {
        return static_cast<D*>(ctx)->IhdaDspIrqEnable(callback);
    }
    // Disable DSP interrupts and clears the callback.
    static void IhdaDspIrqDisable(void* ctx) { static_cast<D*>(ctx)->IhdaDspIrqDisable(); }
};

class IhdaDspProtocolProxy {
public:
    IhdaDspProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    IhdaDspProtocolProxy(const ihda_dsp_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(ihda_dsp_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Fetch the parent HDA controller's PCI device info.
    void GetDevInfo(zx_pcie_device_info_t* out_out) { ops_->get_dev_info(ctx_, out_out); }
    // Fetch a VMO that represents the BAR holding the Audio DSP registers.
    zx_status_t GetMmio(zx_handle_t* out_vmo, size_t* out_size) {
        return ops_->get_mmio(ctx_, out_vmo, out_size);
    }
    // Fetch a handle to our bus transaction initiator.
    zx_status_t GetBti(zx_handle_t* out_bti) { return ops_->get_bti(ctx_, out_bti); }
    // Enables DSP
    void Enable() { ops_->enable(ctx_); }
    // Disable DSP
    void Disable() { ops_->disable(ctx_); }
    // Enables DSP interrupts and set a callback to be invoked when an interrupt is
    // raised.
    // Returns `ZX_ERR_ALREADY_EXISTS` if a callback is already set.
    zx_status_t IrqEnable(const ihda_dsp_irq_t* callback) {
        return ops_->irq_enable(ctx_, callback);
    }
    // Disable DSP interrupts and clears the callback.
    void IrqDisable() { ops_->irq_disable(ctx_); }

private:
    ihda_dsp_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
