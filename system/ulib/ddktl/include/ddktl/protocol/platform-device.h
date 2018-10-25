// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_device.banjo INSTEAD.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "platform-device-internal.h"

// DDK pdev-protocol support
//
// :: Proxies ::
//
// ddk::PDevProtocolProxy is a simple wrapper around
// pdev_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::PDevProtocol is a mixin class that simplifies writing DDK drivers
// that implement the pdev protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PDEV device.
// class PDevDevice {
// using PDevDeviceType = ddk::Device<PDevDevice, /* ddk mixins */>;
//
// class PDevDevice : public PDevDeviceType,
//                    public ddk::PDevProtocol<PDevDevice> {
//   public:
//     PDevDevice(zx_device_t** parent)
//         : PDevDeviceType("my-pdev-protocol-device", parent) {}
//
//     zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
//
//     zx_status_t PDevMapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr_buffer,
//     size_t* vaddr_size, uint64_t* out_paddr, zx_handle_t* out_handle);
//
//     zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_irq);
//
//     zx_status_t PDevGetBti(uint32_t index, zx_handle_t* out_bti);
//
//     zx_status_t PDevGetSmc(uint32_t index, zx_handle_t* out_smc);
//
//     zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
//
//     zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
//
//     zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args, zx_device_t**
//     out_device);
//
//     zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_out_protocol_buffer,
//     size_t out_protocol_size, size_t* out_out_protocol_actual);
//
//     ...
// };

namespace ddk {

template <typename D>
class PDevProtocol : public internal::base_protocol {
public:
    PDevProtocol() {
        internal::CheckPDevProtocolSubclass<D>();
        ops_.get_mmio = PDevGetMmio;
        ops_.map_mmio = PDevMapMmio;
        ops_.get_interrupt = PDevGetInterrupt;
        ops_.get_bti = PDevGetBti;
        ops_.get_smc = PDevGetSmc;
        ops_.get_device_info = PDevGetDeviceInfo;
        ops_.get_board_info = PDevGetBoardInfo;
        ops_.device_add = PDevDeviceAdd;
        ops_.get_protocol = PDevGetProtocol;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_PDEV;
        ddk_proto_ops_ = &ops_;
    }

protected:
    pdev_protocol_ops_t ops_ = {};

private:
    static zx_status_t PDevGetMmio(void* ctx, uint32_t index, pdev_mmio_t* out_mmio) {
        return static_cast<D*>(ctx)->PDevGetMmio(index, out_mmio);
    }
    static zx_status_t PDevMapMmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                   void** out_vaddr_buffer, size_t* vaddr_size, uint64_t* out_paddr,
                                   zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->PDevMapMmio(index, cache_policy, out_vaddr_buffer, vaddr_size,
                                                 out_paddr, out_handle);
    }
    static zx_status_t PDevGetInterrupt(void* ctx, uint32_t index, uint32_t flags,
                                        zx_handle_t* out_irq) {
        return static_cast<D*>(ctx)->PDevGetInterrupt(index, flags, out_irq);
    }
    static zx_status_t PDevGetBti(void* ctx, uint32_t index, zx_handle_t* out_bti) {
        return static_cast<D*>(ctx)->PDevGetBti(index, out_bti);
    }
    static zx_status_t PDevGetSmc(void* ctx, uint32_t index, zx_handle_t* out_smc) {
        return static_cast<D*>(ctx)->PDevGetSmc(index, out_smc);
    }
    static zx_status_t PDevGetDeviceInfo(void* ctx, pdev_device_info_t* out_info) {
        return static_cast<D*>(ctx)->PDevGetDeviceInfo(out_info);
    }
    static zx_status_t PDevGetBoardInfo(void* ctx, pdev_board_info_t* out_info) {
        return static_cast<D*>(ctx)->PDevGetBoardInfo(out_info);
    }
    static zx_status_t PDevDeviceAdd(void* ctx, uint32_t index, const device_add_args_t* args,
                                     zx_device_t** out_device) {
        return static_cast<D*>(ctx)->PDevDeviceAdd(index, args, out_device);
    }
    static zx_status_t PDevGetProtocol(void* ctx, uint32_t proto_id, uint32_t index,
                                       void* out_out_protocol_buffer, size_t out_protocol_size,
                                       size_t* out_out_protocol_actual) {
        return static_cast<D*>(ctx)->PDevGetProtocol(proto_id, index, out_out_protocol_buffer,
                                                     out_protocol_size, out_out_protocol_actual);
    }
};

class PDevProtocolProxy {
public:
    PDevProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    PDevProtocolProxy(const pdev_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(pdev_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
        return ops_->get_mmio(ctx_, index, out_mmio);
    }
    zx_status_t MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr_buffer,
                        size_t* vaddr_size, uint64_t* out_paddr, zx_handle_t* out_handle) {
        return ops_->map_mmio(ctx_, index, cache_policy, out_vaddr_buffer, vaddr_size, out_paddr,
                              out_handle);
    }
    zx_status_t GetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_irq) {
        return ops_->get_interrupt(ctx_, index, flags, out_irq);
    }
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_bti) {
        return ops_->get_bti(ctx_, index, out_bti);
    }
    zx_status_t GetSmc(uint32_t index, zx_handle_t* out_smc) {
        return ops_->get_smc(ctx_, index, out_smc);
    }
    zx_status_t GetDeviceInfo(pdev_device_info_t* out_info) {
        return ops_->get_device_info(ctx_, out_info);
    }
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info) {
        return ops_->get_board_info(ctx_, out_info);
    }
    zx_status_t DeviceAdd(uint32_t index, const device_add_args_t* args, zx_device_t** out_device) {
        return ops_->device_add(ctx_, index, args, out_device);
    }
    zx_status_t GetProtocol(uint32_t proto_id, uint32_t index, void* out_out_protocol_buffer,
                            size_t out_protocol_size, size_t* out_out_protocol_actual) {
        return ops_->get_protocol(ctx_, proto_id, index, out_out_protocol_buffer, out_protocol_size,
                                  out_out_protocol_actual);
    }

private:
    pdev_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
