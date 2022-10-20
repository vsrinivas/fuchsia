// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/bootstrap16.h"

#include <align.h>
#include <assert.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <fbl/algorithm.h>
#include <kernel/mutex.h>
#include <ktl/iterator.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

static paddr_t bootstrap_phys_addr = UINT64_MAX;
static Mutex bootstrap_lock;
// The bootstrap address space is kept as a global variable in order to maintain ownership of low
// mem PML4. If this aspace were released then the physical pages it holds would be returned to the
// PMM and may be reallocated for other uses. Normally that's fine because we could always ask for
// more pages from the PMM when we need them, but these pages are special in that are "low mem"
// pages that exist in the first 4GB of the physical address space. If we were to release them they
// may get reused for other purposes. Then if we need low mem pages in order to bootstrap a new CPU,
// the PMM may not have any available and we'd be unable to do so.
static fbl::RefPtr<VmAspace> bootstrap_aspace = nullptr;

// Actual GDT address.
extern uint8_t _temp_gdt;
extern uint8_t _temp_gdt_end;

void x86_bootstrap16_init(paddr_t bootstrap_base) {
  DEBUG_ASSERT(!IS_PAGE_ALIGNED(bootstrap_phys_addr));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(bootstrap_base));
  DEBUG_ASSERT(bootstrap_base <= (MB)-k_x86_bootstrap16_buffer_size);
  bootstrap_phys_addr = bootstrap_base;
}

zx_status_t x86_bootstrap16_acquire(uintptr_t entry64, void** bootstrap_aperture,
                                    paddr_t* instr_ptr) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Make sure x86_bootstrap16_init has been called, and bail early if not.
  if (!IS_PAGE_ALIGNED(bootstrap_phys_addr)) {
    return ZX_ERR_BAD_STATE;
  }

  // This routine assumes that the bootstrap buffer is 3 pages long.
  static_assert(k_x86_bootstrap16_buffer_size == 3UL * PAGE_SIZE);

  LTRACEF("bootstrap_phys_addr %#lx\n", bootstrap_phys_addr);

  // Make sure the entrypoint code is in the bootstrap code that will be
  // loaded
  if (entry64 < (uintptr_t)x86_bootstrap16_start || entry64 >= (uintptr_t)x86_bootstrap16_end) {
    return ZX_ERR_INVALID_ARGS;
  }

  VmAspace* kernel_aspace = VmAspace::kernel_aspace();
  void* bootstrap_virt_addr = nullptr;

  // Ensure only one caller is using the bootstrap region
  bootstrap_lock.Acquire();

  // Clean up the kernel address space on the way out. The bootstrap address space does not need
  // to be cleaned up since it is kept as a global variable.
  auto cleanup = fit::defer([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
    if (bootstrap_virt_addr) {
      kernel_aspace->FreeRegion(reinterpret_cast<vaddr_t>(bootstrap_virt_addr));
    }
    bootstrap_lock.Release();
  });

  if (!bootstrap_aspace) {
    bootstrap_aspace = VmAspace::Create(VmAspace::Type::LowKernel, "bootstrap16");
    if (!bootstrap_aspace) {
      return ZX_ERR_NO_MEMORY;
    }

    // Bootstrap aspace needs 3 regions mapped:
    // 1) The bootstrap region (identity mapped) which contains:
    // 1.a) A copy of the bootstrap code.
    // 1.b) A copy of the GDT used temporarily to bounce.
    // These next two come implicitly from the shared kernel aspace:
    // 2) The kernel's version of the bootstrap code page (matched mapping)
    // 3) The page containing the aps_still_booting counter (matched mapping)
    void* vaddr = reinterpret_cast<void*>(bootstrap_phys_addr);
    zx_status_t status = bootstrap_aspace->AllocPhysical(
        "bootstrap_mapping", k_x86_bootstrap16_buffer_size, &vaddr, PAGE_SIZE_SHIFT,
        bootstrap_phys_addr, VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);
    if (status != ZX_OK) {
      TRACEF("Failed to create wakeup bootstrap aspace\n");
      return status;
    }
  }

  // Map the AP bootstrap page and a low mem data page to configure
  // the AP processors with
  zx_status_t status = kernel_aspace->AllocPhysical(
      "bootstrap16_aperture", k_x86_bootstrap16_buffer_size,
      &bootstrap_virt_addr,                                 // requested virtual address
      PAGE_SIZE_SHIFT,                                      // alignment log2
      bootstrap_phys_addr,                                  // physical address
      0,                                                    // vmm flags
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);  // arch mmu flags
  if (status != ZX_OK) {
    TRACEF("could not allocate AP bootstrap page: %d\n", status);
    return status;
  }
  DEBUG_ASSERT(bootstrap_virt_addr != nullptr);

  // Copy the bootstrap code and _temp_gdt to the bootstrap buffer. Compute where the offsets are
  // going to be up front.
  const uintptr_t bootstrap_code_len =
      (uintptr_t)x86_bootstrap16_end - (uintptr_t)x86_bootstrap16_start;
  void* const temp_gdt_virt_addr =
      (void*)((uintptr_t)bootstrap_virt_addr + ROUNDUP(bootstrap_code_len, 8));

  const uintptr_t temp_gdt_len = (uintptr_t)&_temp_gdt_end - (uintptr_t)&_temp_gdt;
  DEBUG_ASSERT(temp_gdt_len < UINT16_MAX);

  // make sure the bootstrap code + gdt (aligned to 8 bytes) fits within the first page
  DEBUG_ASSERT((uintptr_t)temp_gdt_virt_addr + temp_gdt_len - (uintptr_t)bootstrap_virt_addr <
               PAGE_SIZE);

  // Copy the bootstrap code in
  memcpy(bootstrap_virt_addr, (const void*)x86_bootstrap16_start, bootstrap_code_len);
  LTRACEF("bootstrap code virt %p phys %#lx len %#lx\n", bootstrap_virt_addr, bootstrap_phys_addr,
          bootstrap_code_len);

  // Copy _temp_gdt to just after the code, aligned to an 8 byte boundary. This is to avoid
  // any issues with the kernel being loaded > 4GB.
  memcpy(temp_gdt_virt_addr, &_temp_gdt, temp_gdt_len);
  const uintptr_t temp_gdt_phys_addr =
      bootstrap_phys_addr + ((uintptr_t)temp_gdt_virt_addr - (uintptr_t)bootstrap_virt_addr);
  LTRACEF("temp_gdt virt %p phys %#lx len %#lx\n", temp_gdt_virt_addr, temp_gdt_phys_addr,
          temp_gdt_len);
  DEBUG_ASSERT(temp_gdt_phys_addr < UINT32_MAX);

  // Configuration data shared with the APs to get them to 64-bit mode stored in the 2nd page
  // of the bootstrap buffer.
  struct x86_bootstrap16_data* bootstrap_data =
      (struct x86_bootstrap16_data*)((uintptr_t)bootstrap_virt_addr + PAGE_SIZE);

  const uintptr_t long_mode_entry =
      bootstrap_phys_addr + (entry64 - (uintptr_t)x86_bootstrap16_start);
  ASSERT(long_mode_entry <= UINT32_MAX);

  // Carve out the 3rd page of the bootstrap physical buffer to hold a copy of the top level
  // page table for the bootstrapping code to use temporarily. Copy the contents of the bootstrap
  // aspace's top level PML4 to this page to make sure it's located in low (<4GB) memory. This
  // is needed when bootstrapping from 32bit to 64bit since the CR3 register is only 32bits wide
  // at the time you have to load it.
  const uint64_t phys_bootstrap_pml4 = bootstrap_phys_addr + 2UL * PAGE_SIZE;
  const uint64_t bootstrap_aspace_pml4 = bootstrap_aspace->arch_aspace().pt_phys();
  void* const phys_bootstrap_pml4_virt = paddr_to_physmap(phys_bootstrap_pml4);
  const void* const bootstrap_aspace_pml4_virt = paddr_to_physmap(bootstrap_aspace_pml4);
  LTRACEF("phys_bootstrap_pml4 %p (%#lx), bootstrap_aspace_pml4 %p (%#lx)\n",
          phys_bootstrap_pml4_virt, phys_bootstrap_pml4, bootstrap_aspace_pml4_virt,
          bootstrap_aspace_pml4);
  DEBUG_ASSERT(phys_bootstrap_pml4_virt && bootstrap_aspace_pml4_virt);
  memcpy(phys_bootstrap_pml4_virt, bootstrap_aspace_pml4_virt, PAGE_SIZE);

  uint64_t phys_kernel_pml4 = VmAspace::kernel_aspace()->arch_aspace().pt_phys();
  ASSERT(phys_kernel_pml4 <= UINT32_MAX);

  bootstrap_data->phys_bootstrap_pml4 = static_cast<uint32_t>(phys_bootstrap_pml4);
  bootstrap_data->phys_kernel_pml4 = static_cast<uint32_t>(phys_kernel_pml4);
  bootstrap_data->phys_gdtr_limit = static_cast<uint16_t>(temp_gdt_len - 1);
  bootstrap_data->phys_gdtr_base = static_cast<uint32_t>(temp_gdt_phys_addr);
  bootstrap_data->phys_long_mode_entry = static_cast<uint32_t>(long_mode_entry);
  bootstrap_data->long_mode_cs = CODE_64_SELECTOR;

  *bootstrap_aperture = (void*)((uintptr_t)bootstrap_virt_addr + PAGE_SIZE);
  *instr_ptr = bootstrap_phys_addr;

  // Cancel the deferred cleanup, since we're returning the new aspace and
  // region.
  // NOTE: Since we cancel the cleanup, we are not releasing |bootstrap_lock|.
  // This is released in x86_bootstrap16_release() when the caller is done
  // with the bootstrap region.
  cleanup.cancel();

  return ZX_OK;
}

void x86_bootstrap16_release(void* bootstrap_aperture) TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(bootstrap_aperture);
  DEBUG_ASSERT(bootstrap_lock.IsHeld());
  VmAspace* kernel_aspace = VmAspace::kernel_aspace();
  uintptr_t addr = reinterpret_cast<uintptr_t>(bootstrap_aperture) - PAGE_SIZE;
  kernel_aspace->FreeRegion(addr);

  bootstrap_lock.Release();
}
