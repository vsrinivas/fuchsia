// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_AHCI_TEST_FAKE_BUS_H_
#define SRC_STORAGE_BLOCK_DRIVERS_AHCI_TEST_FAKE_BUS_H_

#include <lib/sync/completion.h>

#include <vector>

#include "../ahci.h"
#include "../bus.h"
#include "../port.h"

namespace ahci {

// Fake bus for unit testing the AHCI driver.

struct FakePort {
  uint32_t num = 0;
  union {
    ahci_port_reg_t reg{};
    uint32_t raw[sizeof(ahci_port_reg_t) / sizeof(uint32_t)];
  };
  union {
    ahci_cl_t* cl_ = nullptr;
    uintptr_t cl_raw;
  };
  union {
    ahci_fis_t* fis_ = nullptr;
    uintptr_t fis_raw;
  };
  zx_status_t Read(size_t offset, uint32_t* val_out);
  zx_status_t Write(size_t offset, uint32_t val);
};

class FakeBus : public Bus {
 public:
  FakeBus();
  virtual ~FakeBus() override;
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

  virtual void* mmio() override { return nullptr; }

  // Test control functions.

  // Cause calls to Configure() to return an error.
  void DoFailConfigure() { fail_configure_ = true; }

  // Override a register value without going through the normal Write path.
  void PortRegOverride(uint32_t port, size_t offset, uint32_t value) {
    port_[port].raw[offset / sizeof(uint32_t)] = value;
  }

 private:
  zx_status_t HbaRead(size_t offset, uint32_t* val_out);
  zx_status_t HbaWrite(size_t offset, uint32_t val);

  sync_completion_t irq_completion_;
  bool interrupt_cancelled_ = false;

  bool fail_configure_ = false;

  uint32_t slots_ = 32;
  uint32_t num_ports_ = 4;

  // Fake host bus adapter registers.
  uint32_t ghc_ = 0;

  // Array of unique pointers to allocated io bufs.
  std::vector<std::unique_ptr<ahci_port_mem_t>> iobufs_;

  FakePort port_[AHCI_MAX_PORTS];
};

}  // namespace ahci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_AHCI_TEST_FAKE_BUS_H_
