// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_FAKE_MSR_ACCESS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_FAKE_MSR_ACCESS_H_

#include <zircon/types.h>

#include <arch/x86/platform_access.h>
#include <ktl/array.h>

// A fake implementation of MsrAccess, allowing unit tests to verify MSR reads and writes.
class FakeMsrAccess : public MsrAccess {
 public:
  struct FakeMsr {
    uint32_t index;
    uint64_t value;
  };

  uint64_t read_msr(uint32_t msr_index) override {
    for (uint i = 0; i < msrs_.size(); i++) {
      if (msrs_[i].index == msr_index) {
        return msrs_[i].value;
      }
    }
    PANIC("Attempted to read unknown MSR %#x.", msr_index);
  }

  void write_msr(uint32_t msr_index, uint64_t value) override {
    DEBUG_ASSERT(no_writes_ == false);
    for (uint i = 0; i < msrs_.size(); i++) {
      if (msrs_[i].index == msr_index) {
        msrs_[i].value = value;
        return;
      }
    }
    PANIC("Attempted to write unknown MSR %#x with value %#lx.", msr_index, value);
  }

  ktl::array<FakeMsr, 4> msrs_;
  bool no_writes_ = false;
};

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_FAKE_MSR_ACCESS_H_
