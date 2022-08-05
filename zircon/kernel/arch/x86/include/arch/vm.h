// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_VM_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_VM_H_

#include <zircon/compiler.h>

#include <arch/x86.h>
#include <arch/x86/mmu.h>

static inline bool is_kernel_address(vaddr_t va) {
  return (va >= (vaddr_t)KERNEL_ASPACE_BASE &&
          va - (vaddr_t)KERNEL_ASPACE_BASE < (vaddr_t)KERNEL_ASPACE_SIZE);
}

static inline bool is_user_accessible(vaddr_t va) {
  // This address refers to userspace if it is in the lower half of the
  // canonical addresses.  IOW - if all of the bits in the canonical address
  // mask are zero.
  return (va & kX86CanonicalAddressMask) == 0;
}

// Check that the continuous range of addresses in [va, va+len) are all
// accessible to the user.
static inline bool is_user_accessible_range(vaddr_t va, size_t len) {
  vaddr_t end;

  // Check for normal overflow which implies the range is not continuous.
  if (add_overflow(va, len, &end)) {
    return false;
  }

  return is_user_accessible(va) && (len == 0 || is_user_accessible(end - 1));
}

// Userspace threads can only set an entry point to userspace addresses, or
// the null pointer (for testing a thread that will always fail).
//
// See docs/concepts/kernel/sysret_problem.md for more details.
static inline bool arch_is_valid_user_pc(vaddr_t pc) {
  return (pc == 0) || (is_user_accessible(pc) && x86_is_vaddr_canonical(pc));
}

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_VM_H_
