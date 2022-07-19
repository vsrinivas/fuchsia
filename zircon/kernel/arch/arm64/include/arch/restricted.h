// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_RESTRICTED_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_RESTRICTED_H_

#include <debug.h>

#include <arch/regs.h>

// ArchRestrictedState
class Arm64ArchRestrictedState final : public ArchRestrictedStateImpl {
 public:
  Arm64ArchRestrictedState() = default;
  ~Arm64ArchRestrictedState() = default;

  // Since we're unimplemented, always nak the pre-entry check
  bool ValidatePreRestrictedEntry() override { return false; }

  // The rest of these should not be called by the restricted mode engine if
  // the Validate check above fails.
  void SaveStatePreRestrictedEntry() override { PANIC_UNIMPLEMENTED; }
  [[noreturn]] void EnterRestricted() override { PANIC_UNIMPLEMENTED; }

  void SaveRestrictedSyscallState(const syscall_regs_t *regs) override { PANIC_UNIMPLEMENTED; }
  [[noreturn]] void EnterFull(uintptr_t vector_table, uintptr_t context, uint64_t code) override {
    PANIC_UNIMPLEMENTED;
  }

  void Dump() override {}
};

using ArchRestrictedState = Arm64ArchRestrictedState;

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_RESTRICTED_H_
