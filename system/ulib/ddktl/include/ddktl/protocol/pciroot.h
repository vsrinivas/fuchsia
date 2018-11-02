// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pciroot.banjo INSTEAD.

#pragma once

#include <ddk/protocol/pciroot.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/hw/pci.h>
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
//     zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* out_info);
//
//     zx_status_t PcirootGetPciIrqInfo(pci_irq_info_t* out_info);
//
//     zx_status_t PcirootDriverShouldProxyConfig(bool* out_use_proxy);
//
//     zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t*
//     out_value);
//
//     zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t*
//     out_value);
//
//     zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t*
//     out_value);
//
//     zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value);
//
//     zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value);
//
//     zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value);
//
//     zx_status_t PcirootMsiAllocBlock(uint64_t requested_irqs, bool can_target_64bit, msi_block_t*
//     out_block);
//
//     zx_status_t PcirootMsiFreeBlock(const msi_block_t* block);
//
//     zx_status_t PcirootMsiMaskUnmask(uint64_t msi_id, bool mask);
//
//     zx_status_t PcirootGetAddressSpace(size_t len, pci_address_space_t type, bool low, uint64_t*
//     out_base);
//
//     zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t len, pci_address_space_t type);
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
        pciroot_protocol_ops_.get_pci_platform_info = PcirootGetPciPlatformInfo;
        pciroot_protocol_ops_.get_pci_irq_info = PcirootGetPciIrqInfo;
        pciroot_protocol_ops_.driver_should_proxy_config = PcirootDriverShouldProxyConfig;
        pciroot_protocol_ops_.config_read8 = PcirootConfigRead8;
        pciroot_protocol_ops_.config_read16 = PcirootConfigRead16;
        pciroot_protocol_ops_.config_read32 = PcirootConfigRead32;
        pciroot_protocol_ops_.config_write8 = PcirootConfigWrite8;
        pciroot_protocol_ops_.config_write16 = PcirootConfigWrite16;
        pciroot_protocol_ops_.config_write32 = PcirootConfigWrite32;
        pciroot_protocol_ops_.msi_alloc_block = PcirootMsiAllocBlock;
        pciroot_protocol_ops_.msi_free_block = PcirootMsiFreeBlock;
        pciroot_protocol_ops_.msi_mask_unmask = PcirootMsiMaskUnmask;
        pciroot_protocol_ops_.get_address_space = PcirootGetAddressSpace;
        pciroot_protocol_ops_.free_address_space = PcirootFreeAddressSpace;
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
    static zx_status_t PcirootGetPciPlatformInfo(void* ctx, pci_platform_info_t* out_info) {
        return static_cast<D*>(ctx)->PcirootGetPciPlatformInfo(out_info);
    }
    static zx_status_t PcirootGetPciIrqInfo(void* ctx, pci_irq_info_t* out_info) {
        return static_cast<D*>(ctx)->PcirootGetPciIrqInfo(out_info);
    }
    static zx_status_t PcirootDriverShouldProxyConfig(void* ctx, bool* out_use_proxy) {
        return static_cast<D*>(ctx)->PcirootDriverShouldProxyConfig(out_use_proxy);
    }
    static zx_status_t PcirootConfigRead8(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                          uint8_t* out_value) {
        return static_cast<D*>(ctx)->PcirootConfigRead8(address, offset, out_value);
    }
    static zx_status_t PcirootConfigRead16(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                           uint16_t* out_value) {
        return static_cast<D*>(ctx)->PcirootConfigRead16(address, offset, out_value);
    }
    static zx_status_t PcirootConfigRead32(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                           uint32_t* out_value) {
        return static_cast<D*>(ctx)->PcirootConfigRead32(address, offset, out_value);
    }
    static zx_status_t PcirootConfigWrite8(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                           uint8_t value) {
        return static_cast<D*>(ctx)->PcirootConfigWrite8(address, offset, value);
    }
    static zx_status_t PcirootConfigWrite16(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                            uint16_t value) {
        return static_cast<D*>(ctx)->PcirootConfigWrite16(address, offset, value);
    }
    static zx_status_t PcirootConfigWrite32(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                            uint32_t value) {
        return static_cast<D*>(ctx)->PcirootConfigWrite32(address, offset, value);
    }
    static zx_status_t PcirootMsiAllocBlock(void* ctx, uint64_t requested_irqs,
                                            bool can_target_64bit, msi_block_t* out_block) {
        return static_cast<D*>(ctx)->PcirootMsiAllocBlock(requested_irqs, can_target_64bit,
                                                          out_block);
    }
    static zx_status_t PcirootMsiFreeBlock(void* ctx, const msi_block_t* block) {
        return static_cast<D*>(ctx)->PcirootMsiFreeBlock(block);
    }
    static zx_status_t PcirootMsiMaskUnmask(void* ctx, uint64_t msi_id, bool mask) {
        return static_cast<D*>(ctx)->PcirootMsiMaskUnmask(msi_id, mask);
    }
    static zx_status_t PcirootGetAddressSpace(void* ctx, size_t len, pci_address_space_t type,
                                              bool low, uint64_t* out_base) {
        return static_cast<D*>(ctx)->PcirootGetAddressSpace(len, type, low, out_base);
    }
    static zx_status_t PcirootFreeAddressSpace(void* ctx, uint64_t base, size_t len,
                                               pci_address_space_t type) {
        return static_cast<D*>(ctx)->PcirootFreeAddressSpace(base, len, type);
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
    zx_status_t GetPciPlatformInfo(pci_platform_info_t* out_info) {
        return ops_->get_pci_platform_info(ctx_, out_info);
    }
    zx_status_t GetPciIrqInfo(pci_irq_info_t* out_info) {
        return ops_->get_pci_irq_info(ctx_, out_info);
    }
    zx_status_t DriverShouldProxyConfig(bool* out_use_proxy) {
        return ops_->driver_should_proxy_config(ctx_, out_use_proxy);
    }
    zx_status_t ConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* out_value) {
        return ops_->config_read8(ctx_, address, offset, out_value);
    }
    zx_status_t ConfigRead16(const pci_bdf_t* address, uint16_t offset, uint16_t* out_value) {
        return ops_->config_read16(ctx_, address, offset, out_value);
    }
    zx_status_t ConfigRead32(const pci_bdf_t* address, uint16_t offset, uint32_t* out_value) {
        return ops_->config_read32(ctx_, address, offset, out_value);
    }
    zx_status_t ConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value) {
        return ops_->config_write8(ctx_, address, offset, value);
    }
    zx_status_t ConfigWrite16(const pci_bdf_t* address, uint16_t offset, uint16_t value) {
        return ops_->config_write16(ctx_, address, offset, value);
    }
    zx_status_t ConfigWrite32(const pci_bdf_t* address, uint16_t offset, uint32_t value) {
        return ops_->config_write32(ctx_, address, offset, value);
    }
    zx_status_t MsiAllocBlock(uint64_t requested_irqs, bool can_target_64bit,
                              msi_block_t* out_block) {
        return ops_->msi_alloc_block(ctx_, requested_irqs, can_target_64bit, out_block);
    }
    zx_status_t MsiFreeBlock(const msi_block_t* block) { return ops_->msi_free_block(ctx_, block); }
    zx_status_t MsiMaskUnmask(uint64_t msi_id, bool mask) {
        return ops_->msi_mask_unmask(ctx_, msi_id, mask);
    }
    zx_status_t GetAddressSpace(size_t len, pci_address_space_t type, bool low,
                                uint64_t* out_base) {
        return ops_->get_address_space(ctx_, len, type, low, out_base);
    }
    zx_status_t FreeAddressSpace(uint64_t base, size_t len, pci_address_space_t type) {
        return ops_->free_address_space(ctx_, base, len, type);
    }

private:
    pciroot_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
