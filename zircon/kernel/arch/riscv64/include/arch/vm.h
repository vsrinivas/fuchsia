// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_VM_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_VM_H_

#include <vm/vm.h>

// Userspace threads can only set an entry point to userspace addresses, or
// the null pointer (for testing a thread that will always fail).
static inline bool arch_is_valid_user_pc(vaddr_t pc) {
  return (pc == 0) || (is_user_address(pc) && !is_kernel_address(pc));
}

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_VM_H_
