// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pci-bus.h"

#include <lib/ddk/debug.h>
#include <lib/device-protocol/pci.h>
#include <lib/pci/hw.h>

#include "fuchsia/hardware/pci/c/banjo.h"

namespace ahci {

PciBus::~PciBus() {}

zx_status_t PciBus::RegRead(size_t offset, uint32_t* val_out) {
  *val_out = le32toh(mmio_->Read32(offset));
  return ZX_OK;
}

zx_status_t PciBus::RegWrite(size_t offset, uint32_t val) {
  mmio_->Write32(htole32(val), offset);
  return ZX_OK;
}

zx_status_t PciBus::Configure(zx_device_t* parent) {
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  zx_device_t* fragment = nullptr;
  if (!device_get_fragment(parent, "pci", &fragment) ||
      (status = device_get_protocol(fragment, ZX_PROTOCOL_PCI, &pci_)) != ZX_OK) {
    zxlogf(ERROR, "ahci: error getting pci config information");
    return status;
  }

  // Map register window.
  mmio_buffer_t buf;
  status = pci_map_bar_buffer(&pci_, 5u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d mapping pci register window", status);
    return status;
  }
  mmio_ = ddk::MmioBuffer(buf);

  pcie_device_info_t config;
  status = pci_get_device_info(&pci_, &config);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error getting pci config information");
    return status;
  }

  // TODO: move this to SATA.
  if (config.sub_class != 0x06 && config.base_class == 0x01) {  // SATA
    zxlogf(ERROR, "ahci: device class 0x%x unsupported", config.sub_class);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // FIXME intel devices need to set SATA port enable at config + 0x92
  // ahci controller is bus master
  status = pci_enable_bus_master(&pci_, true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d enabling bus master", status);
    return status;
  }

  // Request 1 interrupt of any mode.
  status = pci_configure_irq_mode(&pci_, 1, &irq_mode_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: no interrupts available %d", status);
    return ZX_ERR_NO_RESOURCES;
  }

  // Get bti handle.
  status = pci_get_bti(&pci_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d getting bti handle", status);
    return status;
  }

  // Get irq handle.
  status = pci_map_interrupt(&pci_, 0, irq_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d getting irq handle", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t PciBus::IoBufferInit(io_buffer_t* buffer_, size_t size, uint32_t flags,
                                 zx_paddr_t* phys_out, void** virt_out) {
  // Allocate memory for the command list, FIS receive area, command table and PRDT.
  zx_status_t status = io_buffer_init(buffer_, bti_.get(), size, flags);
  if (status != ZX_OK) {
    return status;
  }
  *phys_out = io_buffer_phys(buffer_);
  *virt_out = io_buffer_virt(buffer_);
  return ZX_OK;
}

zx_status_t PciBus::BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset,
                           uint64_t size, zx_paddr_t* addrs, size_t addrs_count, zx::pmt* pmt_out) {
  zx_handle_t pmt;
  zx_status_t status =
      zx_bti_pin(bti_.get(), options, vmo->get(), offset, size, addrs, addrs_count, &pmt);
  if (status == ZX_OK) {
    *pmt_out = zx::pmt(pmt);
  }
  return status;
}

zx_status_t PciBus::InterruptWait() {
  if (irq_mode_ == PCI_IRQ_MODE_LEGACY) {
    pci_ack_interrupt(&pci_);
  }

  return irq_.wait(nullptr);
}

void PciBus::InterruptCancel() { irq_.destroy(); }

}  // namespace ahci
