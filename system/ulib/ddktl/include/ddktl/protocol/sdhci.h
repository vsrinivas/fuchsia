// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdhci.banjo INSTEAD.

#pragma once

#include <ddk/protocol/sdhci.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "sdhci-internal.h"

// DDK sdhci-protocol support
//
// :: Proxies ::
//
// ddk::SdhciProtocolProxy is a simple wrapper around
// sdhci_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::SdhciProtocol is a mixin class that simplifies writing DDK drivers
// that implement the sdhci protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SDHCI device.
// class SdhciDevice {
// using SdhciDeviceType = ddk::Device<SdhciDevice, /* ddk mixins */>;
//
// class SdhciDevice : public SdhciDeviceType,
//                     public ddk::SdhciProtocol<SdhciDevice> {
//   public:
//     SdhciDevice(zx_device_t* parent)
//         : SdhciDeviceType("my-sdhci-protocol-device", parent) {}
//
//     zx_status_t SdhciGetInterrupt(zx_handle_t* out_irq);
//
//     zx_status_t SdhciGetMmio(zx_handle_t* out_mmio);
//
//     zx_status_t SdhciGetBti(uint32_t index, zx_handle_t* out_bti);
//
//     uint32_t SdhciGetBaseClock();
//
//     uint64_t SdhciGetQuirks();
//
//     void SdhciHwReset();
//
//     ...
// };

namespace ddk {

template <typename D>
class SdhciProtocol : public internal::base_mixin {
public:
    SdhciProtocol() {
        internal::CheckSdhciProtocolSubclass<D>();
        sdhci_protocol_ops_.get_interrupt = SdhciGetInterrupt;
        sdhci_protocol_ops_.get_mmio = SdhciGetMmio;
        sdhci_protocol_ops_.get_bti = SdhciGetBti;
        sdhci_protocol_ops_.get_base_clock = SdhciGetBaseClock;
        sdhci_protocol_ops_.get_quirks = SdhciGetQuirks;
        sdhci_protocol_ops_.hw_reset = SdhciHwReset;
    }

protected:
    sdhci_protocol_ops_t sdhci_protocol_ops_ = {};

private:
    static zx_status_t SdhciGetInterrupt(void* ctx, zx_handle_t* out_irq) {
        return static_cast<D*>(ctx)->SdhciGetInterrupt(out_irq);
    }
    static zx_status_t SdhciGetMmio(void* ctx, zx_handle_t* out_mmio) {
        return static_cast<D*>(ctx)->SdhciGetMmio(out_mmio);
    }
    // Gets a handle to the bus transaction initiator for the device. The caller
    // receives ownership of the handle.
    static zx_status_t SdhciGetBti(void* ctx, uint32_t index, zx_handle_t* out_bti) {
        return static_cast<D*>(ctx)->SdhciGetBti(index, out_bti);
    }
    static uint32_t SdhciGetBaseClock(void* ctx) {
        return static_cast<D*>(ctx)->SdhciGetBaseClock();
    }
    // returns device quirks
    static uint64_t SdhciGetQuirks(void* ctx) { return static_cast<D*>(ctx)->SdhciGetQuirks(); }
    // platform specific HW reset
    static void SdhciHwReset(void* ctx) { static_cast<D*>(ctx)->SdhciHwReset(); }
};

class SdhciProtocolProxy {
public:
    SdhciProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    SdhciProtocolProxy(const sdhci_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(sdhci_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetInterrupt(zx_handle_t* out_irq) { return ops_->get_interrupt(ctx_, out_irq); }
    zx_status_t GetMmio(zx_handle_t* out_mmio) { return ops_->get_mmio(ctx_, out_mmio); }
    // Gets a handle to the bus transaction initiator for the device. The caller
    // receives ownership of the handle.
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_bti) {
        return ops_->get_bti(ctx_, index, out_bti);
    }
    uint32_t GetBaseClock() { return ops_->get_base_clock(ctx_); }
    // returns device quirks
    uint64_t GetQuirks() { return ops_->get_quirks(ctx_); }
    // platform specific HW reset
    void HwReset() { ops_->hw_reset(ctx_); }

private:
    sdhci_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
