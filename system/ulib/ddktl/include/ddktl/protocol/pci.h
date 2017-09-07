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
// // A driver that communicates with a MX_PROTOCOL_PCI device
// class MyDevice;
// using MyDeviceType = ddk::Device<MyDevice, /* ddk mixins */>;
//
// class MyDevice : public MyDeviceType {
//   public:
//     MyDevice(mx_device_t* parent)
//       : MyDeviceType(parent) {}
//
//     void DdkRelease() {
//         // Clean up
//         delete this;
//     }
//
//     mx_status_t Bind() {
//         pci_protocol_t* ops;
//         auto status = get_device_protocol(parent_, MX_PROTOCOL_PCI,
//                                           reinterpret_cast<void**>(&ops));
//         if (status != MX_OK) {
//             return status;
//         }
//         pci_.reset(new ddk::PciProtocolProxy(ops));
//
//         // Query interrupt capabilities, etc.
//         uint32_t irq_count = 0;
//         if (pci_.QueryIrqModeCaps(MX_PCIE_IRQ_MODE_MSI, &irq_count) == MX_OK) {
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

    mx_status_t MapResource(uint32_t res_id, uint32_t cache_policy, void** vaddr, size_t* size,
                            mx_handle_t* out_handle) {
        return ops_->map_resource(ctx_, res_id, cache_policy, vaddr, size, out_handle);
    }

    mx_status_t EnableBusMaster(bool enable) {
        return ops_->enable_bus_master(ctx_, enable);
    }

    mx_status_t EnablePio(bool enable) {
        return ops_->enable_pio(ctx_, enable);
    }

    mx_status_t ResetDevice() {
        return ops_->reset_device(ctx_);
    }

    mx_status_t MapInterrupt(int which_irq, mx_handle_t* out_handle) {
        return ops_->map_interrupt(ctx_, which_irq, out_handle);
    }

    mx_status_t QueryIrqModeCaps(mx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
        return ops_->query_irq_mode_caps(ctx_, mode, out_max_irqs);
    }

    mx_status_t SetIrqMode(mx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
        return ops_->set_irq_mode(ctx_, mode, requested_irq_count);
    }

    mx_status_t GetDeviceInfo(mx_pcie_device_info_t* out_info) {
        return ops_->get_device_info(ctx_, out_info);
    }

  private:
    pci_protocol_ops_t* ops_;
    void* ctx_;
};

}  // namespace ddk
