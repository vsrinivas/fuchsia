// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HYPERVISOR_INVALIDATE_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HYPERVISOR_INVALIDATE_H_

#include <zircon/types.h>

// INVVPID invalidation types.
//
// From Volume 3, Section 30.3: There are four INVVPID types currently defined:
// * Individual-address invalidation: If the INVVPID type is 0, the logical
//   processor invalidates mappings for the linear address and VPID specified in
//   the INVVPID descriptor. In some cases, it may invalidate mappings for other
//   linear addresses (or other VPIDs) as well.
// * Single-context invalidation: If the INVVPID type is 1, the logical
//   processor invalidates all mappings tagged with the VPID specified in the
//   INVVPID descriptor. In some cases, it may invalidate mappings for other
//   VPIDs as well.
// * All-contexts invalidation: If the INVVPID type is 2, the logical processor
//   invalidates all mappings tagged with all VPIDs except VPID 0000H. In some
//   cases, it may invalidate translations with VPID 0000H as well.
// * Single-context invalidation, retaining global translations: If the INVVPID
//   type is 3, the logical processor invalidates all mappings tagged with the
//   VPID specified in the INVVPID descriptor except global translations. In
//   some cases, it may invalidate global translations (and mappings with other
//   VPIDs) as well.
enum class InvVpid : uint64_t {
  INDIVIDUAL_ADDRESS = 0,
  SINGLE_CONTEXT = 1,
  ALL_CONTEXTS = 2,
  SINGLE_CONTEXT_RETAIN_GLOBALS = 3,
};

void invvpid(InvVpid invalidation, uint16_t vpid, zx_vaddr_t address);

// INVEPT invalidation types.
//
// From Volume 3, Section 30.3: There are two INVEPT types currently defined:
// * Single-context invalidation. If the INVEPT type is 1, the logical
//   processor invalidates all mappings associated with bits 51:12 of the EPT
//   pointer (EPTP) specified in the INVEPT descriptor. It may invalidate other
//   mappings as well.
// * Global invalidation. If the INVEPT type is 2, the logical processor
//   invalidates mappings associated with all EPTPs.
enum class InvEpt : uint64_t {
  SINGLE_CONTEXT = 1,
  GLOBAL = 2,
};

void invept(InvEpt invalidation, uint64_t eptp);

// Returns an EPT pointer based on an EPT PML4 address.
uint64_t ept_pointer_from_pml4(zx_paddr_t ept_pml4);

// Performs CPU invalidations of the EPT TLB state using the given EPT pointer.
// This invalidates on all necessary CPUs and will perform IPIs.
void broadcast_invept(uint64_t eptp);

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HYPERVISOR_INVALIDATE_H_
