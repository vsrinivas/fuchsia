// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/hypervisor/invalidate.h>

#include "vmx_cpu_state_priv.h"

void invvpid(InvVpid invalidation, uint16_t vpid, zx_vaddr_t address) {
  uint8_t err;
  uint64_t descriptor[] = {vpid, address};

  __asm__ __volatile__("invvpid %[descriptor], %[invalidation]"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [descriptor] "m"(descriptor), [invalidation] "r"(invalidation)
                       : "cc");

  ASSERT(!err);
}

void invept(InvEpt invalidation, uint64_t eptp) {
  uint8_t err;
  uint64_t descriptor[] = {eptp, 0};

  __asm__ __volatile__("invept %[descriptor], %[invalidation]"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [descriptor] "m"(descriptor), [invalidation] "r"(invalidation)
                       : "cc");

  ASSERT(!err);
}

uint64_t ept_pointer_from_pml4(zx_paddr_t ept_pml4) {
  return
      // Physical address of the PML4 page, page aligned.
      ept_pml4 |
      // Use write-back memory type for paging structures.
      VMX_MEMORY_TYPE_WRITE_BACK << 0 |
      // Page walk length of 4 (defined as N minus 1).
      3u << 3;
}
