// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_AHCI_BUS_H_
#define SRC_STORAGE_BLOCK_DRIVERS_AHCI_BUS_H_

#include <lib/zx/pmt.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <fbl/macros.h>

namespace ahci {

class Bus {
 public:
  virtual ~Bus() {}

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Bus);

  // Configure the bus for use. Registers should be accessible after this call.
  virtual zx_status_t Configure(zx_device_t* parent) = 0;

  // Initialize io_buffer, returning the mapped physical and virtual addresses.
  // In parameters are the same as those of io_buffer_init().
  virtual zx_status_t IoBufferInit(io_buffer_t* buffer_, size_t size, uint32_t flags,
                                   zx_paddr_t* phys_out, void** virt_out) = 0;

  // Pin a set of pages for bus transaction initiators (if supported).
  // Parameters the same as zx_bti_pin();
  virtual zx_status_t BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset,
                             uint64_t size, zx_paddr_t* addrs, size_t addrs_count,
                             zx::pmt* pmt_out) = 0;

  // Read or write a 32-bit register.
  // If the bus encounters an error, non-error status will be returned.
  // A bus error typically means the device is no longer accessible. This may be due to hot-
  // unplug and should be handled gracefully.
  virtual zx_status_t RegRead(size_t offset, uint32_t* val_out) = 0;
  virtual zx_status_t RegWrite(size_t offset, uint32_t val) = 0;

  // Wait on an interrupt from the bus's interrupt source.
  virtual zx_status_t InterruptWait() = 0;
  // Cancel a pending interrupt wait.
  virtual void InterruptCancel() = 0;

  // Get a pointer to the virtual mapping of the mmio region.
  // Ownership is retained by the bus, pointer is valid until bus is destroyed.
  virtual void* mmio() = 0;

  // Non-virtual functions.

  // Wait until all bits in |mask| are cleared in |reg| or timeout expires.
  zx_status_t WaitForClear(size_t offset, uint32_t mask, zx::duration timeout);
  // Wait until one bit in |mask| is set in |reg| or timeout expires.
  zx_status_t WaitForSet(size_t offset, uint32_t mask, zx::duration timeout);

 protected:
  Bus() {}
};

}  // namespace ahci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_AHCI_BUS_H_
