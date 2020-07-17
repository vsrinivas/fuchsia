// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_AHCI_PCI_BUS_H_
#define SRC_STORAGE_BLOCK_DRIVERS_AHCI_PCI_BUS_H_

#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/types.h>

#include <ddk/protocol/pci.h>

#include "bus.h"

namespace ahci {

class PciBus : public Bus {
 public:
  virtual ~PciBus() override;
  virtual zx_status_t Configure(zx_device_t* parent) override;
  virtual zx_status_t IoBufferInit(io_buffer_t* buffer_, size_t size, uint32_t flags,
                                   zx_paddr_t* phys_out, void** virt_out) override;
  virtual zx_status_t BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset,
                             uint64_t size, zx_paddr_t* addrs, size_t addrs_count,
                             zx::pmt* pmt_out) override;

  virtual zx_status_t RegRead(size_t offset, uint32_t* val_out) override;
  virtual zx_status_t RegWrite(size_t offset, uint32_t val) override;

  virtual zx_status_t InterruptWait() override;
  virtual void InterruptCancel() override;

  // TODO(fxb/56253): Add MMIO_PTR to cast.
  virtual void* mmio() override { return (void*)mmio_->get(); }

 private:
  pci_protocol_t pci_;
  std::optional<ddk::MmioBuffer> mmio_;
  zx::bti bti_;
  zx::interrupt irq_;
};

}  // namespace ahci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_AHCI_PCI_BUS_H_
