// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_RESTRICTED_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_RESTRICTED_H_

#include <kernel/restricted_state.h>

// ArchRestrictedState
class X86ArchRestrictedState final : public ArchRestrictedStateImpl {
 public:
  X86ArchRestrictedState() = default;
  ~X86ArchRestrictedState() = default;

  bool ValidatePreRestrictedEntry() override;
  void SaveStatePreRestrictedEntry() override;
  [[noreturn]] void EnterRestricted() override;

  void SaveRestrictedSyscallState(const syscall_regs_t *regs) override;
  [[noreturn]] void EnterFull(uintptr_t vector_table, uintptr_t context, uint64_t code) override;

  void Dump() override;

 private:
  uint64_t normal_fs_base_ = 0;
  uint64_t normal_gs_base_ = 0;
};

using ArchRestrictedState = X86ArchRestrictedState;

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_RESTRICTED_H_
