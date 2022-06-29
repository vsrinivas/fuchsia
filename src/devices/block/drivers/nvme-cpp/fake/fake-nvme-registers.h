// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_FAKE_NVME_REGISTERS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_FAKE_NVME_REGISTERS_H_

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio-buffer.h>

#include <functional>
#include <vector>

#include "src/devices/block/drivers/nvme-cpp/registers.h"

namespace fake_nvme {

struct NvmeRegisterCallbacks {
  std::function<void(nvme::ControllerConfigReg&)> set_config;
  std::function<void(bool enable, nvme::InterruptReg&)> interrupt_mask_update;
  std::function<void(bool is_submit, size_t queue_id, nvme::DoorbellReg&)> doorbell_ring;
  std::function<void()> admin_queue_update;
};

// Implements fake MMIO support for the NVMe controller registers.
class FakeNvmeRegisters {
 public:
  FakeNvmeRegisters();
  fdf::MmioBuffer GetBuffer() {
    return fdf::MmioBuffer{
        mmio_buffer_t{
            .vaddr = FakeMmioPtr(this),
            .offset = 0,
            .size = nvme::NVME_REG_DOORBELL_BASE + 0x100,
            .vmo = ZX_HANDLE_INVALID,
        },
        &kMmioOps,
        this,
    };
  }

  void SetCallbacks(NvmeRegisterCallbacks* callbacks) { callbacks_ = callbacks; }
  void SetUpDoorbells(size_t index) {
    if (completion_doorbells_.size() - 1 < index) {
      completion_doorbells_.resize(index + 1);
    }
    if (submission_doorbells_.size() - 1 < index) {
      submission_doorbells_.resize(index + 1);
    }
  }

  nvme::ControllerStatusReg& csts() { return csts_; }

 private:
  nvme::CapabilityReg caps_;
  nvme::VersionReg vers_;
  nvme::InterruptReg interrupt_mask_set_;
  nvme::InterruptReg interrupt_mask_clear_;
  nvme::ControllerConfigReg ccfg_;
  nvme::ControllerStatusReg csts_;
  nvme::AdminQueueAttributesReg admin_queue_attrs_;
  nvme::AdminQueueAddressReg admin_submission_queue_;
  nvme::AdminQueueAddressReg admin_completion_queue_;

  std::vector<nvme::DoorbellReg> completion_doorbells_{1};
  std::vector<nvme::DoorbellReg> submission_doorbells_{1};

  // Define read/write for |bits| that just crashes.
#define STUB_IO_OP(bits)                                                                        \
  static void Write##bits(const void* ctx, const mmio_buffer_t& mmio, uint##bits##_t val,       \
                          zx_off_t offs) {                                                      \
    ZX_ASSERT(false);                                                                           \
  }                                                                                             \
  static uint##bits##_t Read##bits(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) { \
    ZX_ASSERT(false);                                                                           \
  }

  STUB_IO_OP(16)
  STUB_IO_OP(8)
#undef STUB_IO_OP

  static void Write64(const void* ctx, const mmio_buffer_t& mmio, uint64_t val, zx_off_t offs) {
    const_cast<FakeNvmeRegisters*>(static_cast<const FakeNvmeRegisters*>(ctx))->Write64(val, offs);
  }
  static uint64_t Read64(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    return const_cast<FakeNvmeRegisters*>(static_cast<const FakeNvmeRegisters*>(ctx))->Read64(offs);
  }
  void Write64(uint64_t val, zx_off_t offs);
  uint64_t Read64(zx_off_t offs);

  static void Write32(const void* ctx, const mmio_buffer_t& mmio, uint32_t val, zx_off_t offs) {
    const_cast<FakeNvmeRegisters*>(static_cast<const FakeNvmeRegisters*>(ctx))->Write32(val, offs);
  }
  static uint32_t Read32(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    return const_cast<FakeNvmeRegisters*>(static_cast<const FakeNvmeRegisters*>(ctx))->Read32(offs);
  }
  void Write32(uint32_t val, zx_off_t offs);
  uint32_t Read32(zx_off_t offs);

  static constexpr fdf::internal::MmioBufferOps kMmioOps = {
      .Read8 = Read8,
      .Read16 = Read16,
      .Read32 = Read32,
      .Read64 = Read64,
      .Write8 = Write8,
      .Write16 = Write16,
      .Write32 = Write32,
      .Write64 = Write64,
  };
  NvmeRegisterCallbacks* callbacks_ = nullptr;
};

}  // namespace fake_nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_FAKE_NVME_REGISTERS_H_
