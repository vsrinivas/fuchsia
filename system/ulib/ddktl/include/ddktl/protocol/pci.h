// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pci.fidl INSTEAD.

#pragma once

#include <ddk/protocol/pci.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

#include "pci-internal.h"

// DDK pci-protocol support
//
// :: Proxies ::
//
// ddk::PciProtocolProxy is a simple wrapper around
// pci_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::PciProtocol is a mixin class that simplifies writing DDK drivers
// that implement the pci protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_PCI device.
// class PciDevice {
// using PciDeviceType = ddk::Device<PciDevice, /* ddk mixins */>;
//
// class PciDevice : public PciDeviceType,
//                   public ddk::PciProtocol<PciDevice> {
//   public:
//     PciDevice(zx_device_t* parent)
//         : PciDeviceType("my-pci-protocol-device", parent) {}
//
//     zx_status_t PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_res);
//
//     zx_status_t PciMapBar(uint32_t bar_id, uint32_t cache_policy, void** out_vaddr_buffer,
//     size_t* vaddr_size, zx_handle_t* out_handle);
//
//     zx_status_t PciEnableBusMaster(bool enable);
//
//     zx_status_t PciResetDevice();
//
//     zx_status_t PciMapInterrupt(zx_status_t which_irq, zx_handle_t* out_handle);
//
//     zx_status_t PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs);
//
//     zx_status_t PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count);
//
//     zx_status_t PciGetDeviceInfo(zx_pcie_device_info_t* out_into);
//
//     zx_status_t PciConfigRead(uint16_t offset, size_t width, uint32_t* out_value);
//
//     zx_status_t PciConfigWrite(uint16_t offset, size_t width, uint32_t value);
//
//     uint8_t PciGetNextCapability(uint8_t type, uint8_t offset);
//
//     zx_status_t PciGetAuxdata(const char* args, void* out_data_buffer, size_t data_size, size_t*
//     out_data_actual);
//
//     zx_status_t PciGetBti(uint32_t index, zx_handle_t* out_bti);
//
//     ...
// };

namespace ddk {

template <typename D>
class PciProtocol : public internal::base_mixin {
public:
    PciProtocol() {
        internal::CheckPciProtocolSubclass<D>();
        pci_protocol_ops_.get_bar = PciGetBar;
        pci_protocol_ops_.map_bar = PciMapBar;
        pci_protocol_ops_.enable_bus_master = PciEnableBusMaster;
        pci_protocol_ops_.reset_device = PciResetDevice;
        pci_protocol_ops_.map_interrupt = PciMapInterrupt;
        pci_protocol_ops_.query_irq_mode = PciQueryIrqMode;
        pci_protocol_ops_.set_irq_mode = PciSetIrqMode;
        pci_protocol_ops_.get_device_info = PciGetDeviceInfo;
        pci_protocol_ops_.config_read = PciConfigRead;
        pci_protocol_ops_.config_write = PciConfigWrite;
        pci_protocol_ops_.get_next_capability = PciGetNextCapability;
        pci_protocol_ops_.get_auxdata = PciGetAuxdata;
        pci_protocol_ops_.get_bti = PciGetBti;
    }

protected:
    pci_protocol_ops_t pci_protocol_ops_ = {};

private:
    static zx_status_t PciGetBar(void* ctx, uint32_t bar_id, zx_pci_bar_t* out_res) {
        return static_cast<D*>(ctx)->PciGetBar(bar_id, out_res);
    }
    static zx_status_t PciMapBar(void* ctx, uint32_t bar_id, uint32_t cache_policy,
                                 void** out_vaddr_buffer, size_t* vaddr_size,
                                 zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->PciMapBar(bar_id, cache_policy, out_vaddr_buffer, vaddr_size,
                                               out_handle);
    }
    static zx_status_t PciEnableBusMaster(void* ctx, bool enable) {
        return static_cast<D*>(ctx)->PciEnableBusMaster(enable);
    }
    static zx_status_t PciResetDevice(void* ctx) { return static_cast<D*>(ctx)->PciResetDevice(); }
    static zx_status_t PciMapInterrupt(void* ctx, zx_status_t which_irq, zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->PciMapInterrupt(which_irq, out_handle);
    }
    static zx_status_t PciQueryIrqMode(void* ctx, zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
        return static_cast<D*>(ctx)->PciQueryIrqMode(mode, out_max_irqs);
    }
    static zx_status_t PciSetIrqMode(void* ctx, zx_pci_irq_mode_t mode,
                                     uint32_t requested_irq_count) {
        return static_cast<D*>(ctx)->PciSetIrqMode(mode, requested_irq_count);
    }
    static zx_status_t PciGetDeviceInfo(void* ctx, zx_pcie_device_info_t* out_into) {
        return static_cast<D*>(ctx)->PciGetDeviceInfo(out_into);
    }
    static zx_status_t PciConfigRead(void* ctx, uint16_t offset, size_t width,
                                     uint32_t* out_value) {
        return static_cast<D*>(ctx)->PciConfigRead(offset, width, out_value);
    }
    static zx_status_t PciConfigWrite(void* ctx, uint16_t offset, size_t width, uint32_t value) {
        return static_cast<D*>(ctx)->PciConfigWrite(offset, width, value);
    }
    static uint8_t PciGetNextCapability(void* ctx, uint8_t type, uint8_t offset) {
        return static_cast<D*>(ctx)->PciGetNextCapability(type, offset);
    }
    static zx_status_t PciGetAuxdata(void* ctx, const char* args, void* out_data_buffer,
                                     size_t data_size, size_t* out_data_actual) {
        return static_cast<D*>(ctx)->PciGetAuxdata(args, out_data_buffer, data_size,
                                                   out_data_actual);
    }
    static zx_status_t PciGetBti(void* ctx, uint32_t index, zx_handle_t* out_bti) {
        return static_cast<D*>(ctx)->PciGetBti(index, out_bti);
    }
};

class PciProtocolProxy {
public:
    PciProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    PciProtocolProxy(const pci_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(pci_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetBar(uint32_t bar_id, zx_pci_bar_t* out_res) {
        return ops_->get_bar(ctx_, bar_id, out_res);
    }
    zx_status_t MapBar(uint32_t bar_id, uint32_t cache_policy, void** out_vaddr_buffer,
                       size_t* vaddr_size, zx_handle_t* out_handle) {
        return ops_->map_bar(ctx_, bar_id, cache_policy, out_vaddr_buffer, vaddr_size, out_handle);
    }
    zx_status_t EnableBusMaster(bool enable) { return ops_->enable_bus_master(ctx_, enable); }
    zx_status_t ResetDevice() { return ops_->reset_device(ctx_); }
    zx_status_t MapInterrupt(zx_status_t which_irq, zx_handle_t* out_handle) {
        return ops_->map_interrupt(ctx_, which_irq, out_handle);
    }
    zx_status_t QueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
        return ops_->query_irq_mode(ctx_, mode, out_max_irqs);
    }
    zx_status_t SetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
        return ops_->set_irq_mode(ctx_, mode, requested_irq_count);
    }
    zx_status_t GetDeviceInfo(zx_pcie_device_info_t* out_into) {
        return ops_->get_device_info(ctx_, out_into);
    }
    zx_status_t ConfigRead(uint16_t offset, size_t width, uint32_t* out_value) {
        return ops_->config_read(ctx_, offset, width, out_value);
    }
    zx_status_t ConfigWrite(uint16_t offset, size_t width, uint32_t value) {
        return ops_->config_write(ctx_, offset, width, value);
    }
    uint8_t GetNextCapability(uint8_t type, uint8_t offset) {
        return ops_->get_next_capability(ctx_, type, offset);
    }
    zx_status_t GetAuxdata(const char* args, void* out_data_buffer, size_t data_size,
                           size_t* out_data_actual) {
        return ops_->get_auxdata(ctx_, args, out_data_buffer, data_size, out_data_actual);
    }
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_bti) {
        return ops_->get_bti(ctx_, index, out_bti);
    }

private:
    pci_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
