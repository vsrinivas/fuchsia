// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/pci.h>

// DDK PCI protocol support
//
// :: Proxy ::
//
// ddk::PciProtocolProxy is a simple wrapper around pci_protocol_t. It does not own the pointers
// passed to it.
//
// :: Mixin ::
//
// No mixins are defined, as it is not expected that there will be multiple implementations of the
// pci protocol.
//
// :: Example ::
//
// // A driver that communicates with a ZX_PROTOCOL_PCI device
// class MyDevice;
// using MyDeviceType = ddk::Device<MyDevice, /* ddk mixins */>;
//
// class MyDevice : public MyDeviceType {
//   public:
//     MyDevice(zx_device_t* parent)
//       : MyDeviceType(parent) {}
//
//     void DdkRelease() {
//         // Clean up
//         delete this;
//     }
//
//     zx_status_t Bind() {
//         pci_protocol_t* ops;
//         auto status = get_device_protocol(parent_, ZX_PROTOCOL_PCI,
//                                           reinterpret_cast<void**>(&ops));
//         if (status != ZX_OK) {
//             return status;
//         }
//         pci_.reset(new ddk::PciProtocolProxy(ops));
//
//         // Query interrupt capabilities, etc.
//         uint32_t irq_count = 0;
//         if (pci_.QueryIrqModeCaps(ZX_PCIE_IRQ_MODE_MSI, &irq_count) == ZX_OK) {
//             // etc
//         }

//         return DdkAdd("my-device");
//     }
//
//   private:
//     fbl::unique_ptr<ddk::PciProtocolProxy> pci_;
// };

namespace ddk {

class PciProtocolProxy {
  public:
    PciProtocolProxy(pci_protocol_t* proto)
      : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t GetResource(uint32_t res_id, zx_pci_bar_t* out_res) {
        return ops_->get_resource(ctx_, res_id, out_res);
    }

    zx_status_t MapResource(uint32_t res_id, uint32_t cache_policy, void** vaddr, size_t* size,
                            zx_handle_t* out_handle) {
        return ops_->map_resource(ctx_, res_id, cache_policy, vaddr, size, out_handle);
    }

    zx_status_t EnableBusMaster(bool enable) {
        return ops_->enable_bus_master(ctx_, enable);
    }

    zx_status_t ResetDevice() {
        return ops_->reset_device(ctx_);
    }

    zx_status_t MapInterrupt(int which_irq, zx_handle_t* out_handle) {
        return ops_->map_interrupt(ctx_, which_irq, out_handle);
    }

    zx_status_t QueryIrqModeCaps(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
        return ops_->query_irq_mode(ctx_, mode, out_max_irqs);
    }

    zx_status_t SetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
        return ops_->set_irq_mode(ctx_, mode, requested_irq_count);
    }

    zx_status_t GetDeviceInfo(zx_pcie_device_info_t* out_info) {
        return ops_->get_device_info(ctx_, out_info);
    }

    zx_status_t ConfigRead(uint8_t offset, size_t width, uint32_t *value) {
        return ops_->config_read(ctx_, offset, width, value);
    }

    zx_status_t ConfigRead8(uint8_t offset, uint8_t *value) {
        uint32_t val;
        zx_status_t status = ConfigRead(offset, 8, &val);
        *value = val & 0xff;
        return status;
    }

    zx_status_t ConfigRead16(uint8_t offset, uint16_t *value) {
        uint32_t val;
        zx_status_t status = ConfigRead(offset, 16, &val);
        *value = val & 0xffff;
        return status;
    }

    uint8_t GetNextCapability(uint8_t type, uint8_t offset) {
        return ops_->get_next_capability(ctx_, type, offset);
    }

    uint8_t GetFirstCapability(uint8_t type) {
        return GetNextCapability(kPciCfgCapabilitiesPtr - 1u, type);
    }

  private:
    pci_protocol_ops_t* ops_;
    void* ctx_;
};

}  // namespace ddk
