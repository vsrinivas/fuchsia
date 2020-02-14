// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-bus.h"

#include <stdio.h>

#include <fbl/alloc_checker.h>

namespace ahci {

constexpr uint64_t to64(uint64_t upper, uint32_t lower) { return (upper << 32) | lower; }

FakeBus::FakeBus() {
  for (uint32_t i = 0; i < num_ports_; i++) {
    port_[i].num = i;
  }
}

FakeBus::~FakeBus() { iobufs_.clear(); }

zx_status_t FakeBus::Configure(zx_device_t* parent) {
  if (fail_configure_)
    return ZX_ERR_IO;
  return ZX_OK;
}

zx_status_t FakeBus::IoBufferInit(io_buffer_t* buffer_, size_t size, uint32_t flags,
                                  zx_paddr_t* phys_out, void** virt_out) {
  ZX_DEBUG_ASSERT(size == sizeof(ahci_port_mem_t));

  fbl::AllocChecker ac;
  std::unique_ptr<ahci_port_mem_t> mem(new (&ac) ahci_port_mem_t);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  *virt_out = mem.get();
  *phys_out = reinterpret_cast<uintptr_t>(mem.get());
  iobufs_.push_back(std::move(mem));
  return ZX_OK;
}

zx_status_t FakeBus::BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset,
                            uint64_t size, zx_paddr_t* addrs, size_t addrs_count,
                            zx::pmt* pmt_out) {
  return ZX_ERR_IO_NOT_PRESENT;
}

// Read registers in the Host Bus Adapter.
zx_status_t FakeBus::HbaRead(size_t offset, uint32_t* val_out) {
  switch (offset) {
    case kHbaGlobalHostControl:
      *val_out = ghc_;
      return ZX_OK;
    case kHbaCapabilities: {
      *val_out = (0u << 30) |              // Supports native command queue.
                 ((slots_ - 1) << 8) |     // Number of command slots (0-based).
                 ((num_ports_ - 1) << 0);  // Number of ports (0-based).
      return ZX_OK;
    }
    case kHbaPortsImplemented: {
      uint32_t pi = 0;  // Bitfield of available ports.
      // Ports may be hidden by clearing their associated bits.
      for (uint32_t i = 0; i < num_ports_; i++) {
        pi <<= 1;
        pi |= 1;
      }
      *val_out = pi;
      return ZX_OK;
    }

    default:
      ZX_DEBUG_ASSERT(false);
      break;
  }
  return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::HbaWrite(size_t offset, uint32_t val) {
  switch (offset) {
    case kHbaGlobalHostControl:
      if (val & AHCI_GHC_HR) {
        // Reset was asserted. This bit clears asynchronously when reset has succeded.

        // Error to reset if device is enabled.
        if (ghc_ & AHCI_GHC_AE) {
          fprintf(stderr, "FakeBus: reset asserted while device is enabled\n");
        }
        if (val & AHCI_GHC_AE) {
          fprintf(stderr, "FakeBus: reset and enable bits both set\n");
          return ZX_ERR_BAD_STATE;
        }
        // Clear immediately until async response is supported.
        val &= ~AHCI_GHC_HR;
      }
      if (val & AHCI_GHC_AE) {
        // Enabled bit set. This bit reads as set asynchronously when enabled.
        // Leave it set.
      }
      ghc_ = val;
      return ZX_OK;

    default:
      ZX_DEBUG_ASSERT(false);
      return ZX_ERR_IO_NOT_PRESENT;
  }
}

zx_status_t FakeBus::RegRead(size_t offset, uint32_t* val_out) {
  if (offset < kHbaPorts) {
    return HbaRead(offset, val_out);
  }
  // Figure out which port we're talking to.
  offset -= kHbaPorts;  // Get port base address.
  uint32_t port = static_cast<uint32_t>(offset / sizeof(ahci_port_reg_t));
  if (port >= num_ports_) {
    ZX_DEBUG_ASSERT(false);
    return ZX_ERR_IO_NOT_PRESENT;
  }
  offset %= sizeof(ahci_port_reg_t);
  return port_[port].Read(offset, val_out);
}

zx_status_t FakeBus::RegWrite(size_t offset, uint32_t val) {
  if (offset < kHbaPorts) {
    return HbaWrite(offset, val);
  }
  offset -= kHbaPorts;  // Get port base address.
  uint32_t port = static_cast<uint32_t>(offset / sizeof(ahci_port_reg_t));
  if (port >= num_ports_) {
    ZX_DEBUG_ASSERT(false);
    return ZX_ERR_IO_NOT_PRESENT;
  }
  offset %= sizeof(ahci_port_reg_t);
  return port_[port].Write(offset, val);
}

zx_status_t FakeBus::InterruptWait() {
  sync_completion_wait(&irq_completion_, ZX_TIME_INFINITE);
  sync_completion_reset(&irq_completion_);
  if (interrupt_cancelled_)
    return ZX_ERR_CANCELED;
  return ZX_OK;
}

void FakeBus::InterruptCancel() {
  interrupt_cancelled_ = true;
  sync_completion_signal(&irq_completion_);
}

zx_status_t FakePort::Read(size_t offset, uint32_t* val_out) {
  switch (offset) {
    case kPortCommandListBase:
    case kPortCommandListBaseUpper:
    case kPortFISBase:
    case kPortFISBaseUpper:
    case kPortCommand:
    case kPortInterruptStatus:
    case kPortSataError:
    case kPortCommandIssue:
    case kPortSataActive:
      *val_out = raw[offset / sizeof(uint32_t)];
      return ZX_OK;

    default:
      printf("Unhandled read %zu\n", offset);
      ZX_DEBUG_ASSERT(false);
      return ZX_ERR_IO_NOT_PRESENT;
  }
}

zx_status_t FakePort::Write(size_t offset, uint32_t val) {
  switch (offset) {
    case kPortCommand:
      raw[offset / sizeof(uint32_t)] = val;
      return ZX_OK;

    case kPortCommandListBase:
      // TODO: require 1024 byte alignment.
      reg.clb = val;
      cl_raw = to64(reg.clbu, reg.clb);
      return ZX_OK;

    case kPortCommandListBaseUpper:
      reg.clbu = val;
      cl_raw = to64(reg.clbu, reg.clb);
      return ZX_OK;

    case kPortFISBase:
      // TODO: require 256 byte alignment.
      reg.fb = val;
      fis_raw = to64(reg.fbu, reg.fb);
      return ZX_OK;

    case kPortFISBaseUpper:
      reg.fbu = val;
      fis_raw = to64(reg.fbu, reg.fb);
      return ZX_OK;

    case kPortInterruptStatus:
      // Writing to interrupt status clears those set bits.
      reg.is &= ~val;
      return ZX_OK;

    case kPortSataError:
      reg.serr &= ~val;
      return ZX_OK;

    case kPortCommandIssue:
      reg.ci |= val;  // Set additional command bits without clearing existing.
      return ZX_OK;

    default:
      printf("Unhandled write %zu\n", offset);
      ZX_DEBUG_ASSERT(false);
      return ZX_ERR_IO_NOT_PRESENT;
  }
}

}  // namespace ahci
