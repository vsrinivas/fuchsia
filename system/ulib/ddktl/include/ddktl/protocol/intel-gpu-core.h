// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_gpu_core.fidl INSTEAD.

#pragma once

#include <ddk/protocol/intel-gpu-core.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "intel-gpu-core-internal.h"

// DDK zx-intel-gpu-core-protocol support
//
// :: Proxies ::
//
// ddk::ZxIntelGpuCoreProtocolProxy is a simple wrapper around
// zx_intel_gpu_core_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::ZxIntelGpuCoreProtocol is a mixin class that simplifies writing DDK drivers
// that implement the zx-intel-gpu-core protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ZX_INTEL_GPU_CORE device.
// class ZxIntelGpuCoreDevice {
// using ZxIntelGpuCoreDeviceType = ddk::Device<ZxIntelGpuCoreDevice, /* ddk mixins */>;
//
// class ZxIntelGpuCoreDevice : public ZxIntelGpuCoreDeviceType,
//                              public ddk::ZxIntelGpuCoreProtocol<ZxIntelGpuCoreDevice> {
//   public:
//     ZxIntelGpuCoreDevice(zx_device_t* parent)
//         : ZxIntelGpuCoreDeviceType("my-zx-intel-gpu-core-protocol-device", parent) {}
//
//     zx_status_t ZxIntelGpuCoreReadPciConfig16(uint16_t addr, uint16_t* out_value);
//
//     zx_status_t ZxIntelGpuCoreMapPciMmio(uint32_t pci_bar, void** out_buf_buffer, size_t*
//     buf_size);
//
//     zx_status_t ZxIntelGpuCoreUnmapPciMmio(uint32_t pci_bar);
//
//     zx_status_t ZxIntelGpuCoreGetPciBti(uint32_t index, zx_handle_t* out_bti);
//
//     zx_status_t ZxIntelGpuCoreRegisterInterruptCallback(const zx_intel_gpu_core_interrupt_t*
//     callback, uint32_t interrupt_mask);
//
//     zx_status_t ZxIntelGpuCoreUnregisterInterruptCallback();
//
//     uint64_t ZxIntelGpuCoreGttGetSize();
//
//     zx_status_t ZxIntelGpuCoreGttAlloc(uint64_t page_count, uint64_t* out_addr);
//
//     zx_status_t ZxIntelGpuCoreGttFree(uint64_t addr);
//
//     zx_status_t ZxIntelGpuCoreGttClear(uint64_t addr);
//
//     zx_status_t ZxIntelGpuCoreGttInsert(uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
//     uint64_t page_count);
//
//     ...
// };

namespace ddk {

template <typename D>
class ZxIntelGpuCoreProtocol : public internal::base_mixin {
public:
    ZxIntelGpuCoreProtocol() {
        internal::CheckZxIntelGpuCoreProtocolSubclass<D>();
        zx_intel_gpu_core_protocol_ops_.read_pci_config16 = ZxIntelGpuCoreReadPciConfig16;
        zx_intel_gpu_core_protocol_ops_.map_pci_mmio = ZxIntelGpuCoreMapPciMmio;
        zx_intel_gpu_core_protocol_ops_.unmap_pci_mmio = ZxIntelGpuCoreUnmapPciMmio;
        zx_intel_gpu_core_protocol_ops_.get_pci_bti = ZxIntelGpuCoreGetPciBti;
        zx_intel_gpu_core_protocol_ops_.register_interrupt_callback =
            ZxIntelGpuCoreRegisterInterruptCallback;
        zx_intel_gpu_core_protocol_ops_.unregister_interrupt_callback =
            ZxIntelGpuCoreUnregisterInterruptCallback;
        zx_intel_gpu_core_protocol_ops_.gtt_get_size = ZxIntelGpuCoreGttGetSize;
        zx_intel_gpu_core_protocol_ops_.gtt_alloc = ZxIntelGpuCoreGttAlloc;
        zx_intel_gpu_core_protocol_ops_.gtt_free = ZxIntelGpuCoreGttFree;
        zx_intel_gpu_core_protocol_ops_.gtt_clear = ZxIntelGpuCoreGttClear;
        zx_intel_gpu_core_protocol_ops_.gtt_insert = ZxIntelGpuCoreGttInsert;
    }

protected:
    zx_intel_gpu_core_protocol_ops_t zx_intel_gpu_core_protocol_ops_ = {};

private:
    // Reads 16 bits from pci config space; returned in |value_out|.
    static zx_status_t ZxIntelGpuCoreReadPciConfig16(void* ctx, uint16_t addr,
                                                     uint16_t* out_value) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreReadPciConfig16(addr, out_value);
    }
    // Maps the given |pci_bar|; address returned in |addr_out|, size in bytes returned in
    // |size_out|.
    static zx_status_t ZxIntelGpuCoreMapPciMmio(void* ctx, uint32_t pci_bar, void** out_buf_buffer,
                                                size_t* buf_size) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreMapPciMmio(pci_bar, out_buf_buffer, buf_size);
    }
    // Unmaps the given |pci_bar|.
    static zx_status_t ZxIntelGpuCoreUnmapPciMmio(void* ctx, uint32_t pci_bar) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreUnmapPciMmio(pci_bar);
    }
    // Returns a bus transaction initiator.
    static zx_status_t ZxIntelGpuCoreGetPciBti(void* ctx, uint32_t index, zx_handle_t* out_bti) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreGetPciBti(index, out_bti);
    }
    // Registers the given |callback| to be invoked with parameter |data| when an interrupt occurs
    // matching |interrupt_mask|.
    static zx_status_t ZxIntelGpuCoreRegisterInterruptCallback(
        void* ctx, const zx_intel_gpu_core_interrupt_t* callback, uint32_t interrupt_mask) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreRegisterInterruptCallback(callback,
                                                                             interrupt_mask);
    }
    // Un-registers a previously registered interrupt callback.
    static zx_status_t ZxIntelGpuCoreUnregisterInterruptCallback(void* ctx) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreUnregisterInterruptCallback();
    }
    // Returns the size of the GTT (global translation table) in bytes.
    static uint64_t ZxIntelGpuCoreGttGetSize(void* ctx) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreGttGetSize();
    }
    // Allocates a region of the GTT of the given |page_count|, returning the page-aligned virtual
    // address in |addr_out|.
    static zx_status_t ZxIntelGpuCoreGttAlloc(void* ctx, uint64_t page_count, uint64_t* out_addr) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreGttAlloc(page_count, out_addr);
    }
    // Frees the GTT allocation given by |addr|.
    static zx_status_t ZxIntelGpuCoreGttFree(void* ctx, uint64_t addr) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreGttFree(addr);
    }
    // Clears the page table entries for the GTT allocation given by |addr|.
    static zx_status_t ZxIntelGpuCoreGttClear(void* ctx, uint64_t addr) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreGttClear(addr);
    }
    // Inserts page tables entries for the GTT allocation given by |addr| for the vmo represented by
    // handle |buffer|, at the given |page_offset| and |page_count|. Takes ownership of |buffer|.
    static zx_status_t ZxIntelGpuCoreGttInsert(void* ctx, uint64_t addr, zx_handle_t buffer,
                                               uint64_t page_offset, uint64_t page_count) {
        return static_cast<D*>(ctx)->ZxIntelGpuCoreGttInsert(addr, buffer, page_offset, page_count);
    }
};

class ZxIntelGpuCoreProtocolProxy {
public:
    ZxIntelGpuCoreProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    ZxIntelGpuCoreProtocolProxy(const zx_intel_gpu_core_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(zx_intel_gpu_core_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Reads 16 bits from pci config space; returned in |value_out|.
    zx_status_t ReadPciConfig16(uint16_t addr, uint16_t* out_value) {
        return ops_->read_pci_config16(ctx_, addr, out_value);
    }
    // Maps the given |pci_bar|; address returned in |addr_out|, size in bytes returned in
    // |size_out|.
    zx_status_t MapPciMmio(uint32_t pci_bar, void** out_buf_buffer, size_t* buf_size) {
        return ops_->map_pci_mmio(ctx_, pci_bar, out_buf_buffer, buf_size);
    }
    // Unmaps the given |pci_bar|.
    zx_status_t UnmapPciMmio(uint32_t pci_bar) { return ops_->unmap_pci_mmio(ctx_, pci_bar); }
    // Returns a bus transaction initiator.
    zx_status_t GetPciBti(uint32_t index, zx_handle_t* out_bti) {
        return ops_->get_pci_bti(ctx_, index, out_bti);
    }
    // Registers the given |callback| to be invoked with parameter |data| when an interrupt occurs
    // matching |interrupt_mask|.
    zx_status_t RegisterInterruptCallback(const zx_intel_gpu_core_interrupt_t* callback,
                                          uint32_t interrupt_mask) {
        return ops_->register_interrupt_callback(ctx_, callback, interrupt_mask);
    }
    // Un-registers a previously registered interrupt callback.
    zx_status_t UnregisterInterruptCallback() { return ops_->unregister_interrupt_callback(ctx_); }
    // Returns the size of the GTT (global translation table) in bytes.
    uint64_t GttGetSize() { return ops_->gtt_get_size(ctx_); }
    // Allocates a region of the GTT of the given |page_count|, returning the page-aligned virtual
    // address in |addr_out|.
    zx_status_t GttAlloc(uint64_t page_count, uint64_t* out_addr) {
        return ops_->gtt_alloc(ctx_, page_count, out_addr);
    }
    // Frees the GTT allocation given by |addr|.
    zx_status_t GttFree(uint64_t addr) { return ops_->gtt_free(ctx_, addr); }
    // Clears the page table entries for the GTT allocation given by |addr|.
    zx_status_t GttClear(uint64_t addr) { return ops_->gtt_clear(ctx_, addr); }
    // Inserts page tables entries for the GTT allocation given by |addr| for the vmo represented by
    // handle |buffer|, at the given |page_offset| and |page_count|. Takes ownership of |buffer|.
    zx_status_t GttInsert(uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
                          uint64_t page_count) {
        return ops_->gtt_insert(ctx_, addr, buffer, page_offset, page_count);
    }

private:
    zx_intel_gpu_core_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
