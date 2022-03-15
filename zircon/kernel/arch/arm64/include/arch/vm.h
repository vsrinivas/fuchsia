// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_VM_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_VM_H_

#include <sys/types.h>
#include <zircon/compiler.h>

#include <vm/vm.h>

static inline bool is_kernel_address(vaddr_t va) {
  return (va >= (vaddr_t)KERNEL_ASPACE_BASE &&
          va - (vaddr_t)KERNEL_ASPACE_BASE < (vaddr_t)KERNEL_ASPACE_SIZE);
}

static inline constexpr uint8_t kHighVABit = 55;
static inline constexpr uint64_t kUserBitMask = UINT64_C(1) << kHighVABit;
static inline constexpr uint64_t kTbiBit = 56;
static inline constexpr uint64_t kTbiMask = ~((UINT64_C(1) << kTbiBit) - 1);

// This address refers to userspace if bit 55 is zero.
static inline bool is_user_accessible(vaddr_t va) { return (va & kUserBitMask) == 0; }

// Check that the continuous range of addresses in [va, va+len) are all
// accessible to the user.
static inline bool is_user_accessible_range(vaddr_t va, size_t len) {
  vaddr_t end;

  // Check for normal overflow which implies the range is not continuous.
  if (add_overflow(va, len, &end)) {
    return false;
  }

  // Check that the start and end are accessible to userspace.
  if (!is_user_accessible(va) || (len != 0 && !is_user_accessible(end - 1))) {
    return false;
  }

  // Cover the corner case where the start and end are accessible
  // (bit 55 == 0), but there could be a value within the range that could have
  // bit 55 == 1. In this case, the difference between start and end must be
  // at least 2^55.
  if (len >= kUserBitMask) {
    return false;
  }

  return true;
}

// Userspace threads can only set an entry point to userspace addresses, or
// the null pointer (for testing a thread that will always fail).
static inline bool arch_is_valid_user_pc(vaddr_t pc) {
  return (pc == 0) || (is_user_accessible(pc) && !is_kernel_address(pc));
}

static inline uintptr_t arch_detag_ptr(uintptr_t ptr) { return ptr & ~kTbiMask; }

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_VM_H_
