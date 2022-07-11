// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <arch/riscv64/mmu.h>
#include <vm/bootalloc.h>
#include <vm/physmap.h>

// The structure of the RISC-V Page Table Entry (PTE) and the algorithm that is
// used is described in the RISC-V Privileged Spec in section 4.3.2, page 70.
//
// https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMFDQC-and-Priv-v1.11/riscv-privileged-20190608.pdf
//
// Note that index levels in this code are reverse to the index levels found
// in the spec to make this code more consistent with other zircon platforms.
// In the RISC-V spec, for sv39, the top level of the page table tree is level 2
// whereas in the code below it is level 0.

// Level 0 PTEs may point to large pages of size 1GB.
//const uintptr_t l0_large_page_size = 1UL << (PAGE_SIZE_SHIFT + 2 * RISCV64_MMU_PT_SHIFT);
//const uintptr_t l0_large_page_size_mask = l0_large_page_size - 1;

// Level 1 PTEs may point to large pages of size 2MB.
const uintptr_t l1_large_page_size = 1UL << (PAGE_SIZE_SHIFT + RISCV64_MMU_PT_SHIFT);
const uintptr_t l1_large_page_size_mask = l1_large_page_size - 1;

// Extract the level 0 physical page number from the virtual address.  In the
// RISC-V spec this corresponds to va.vpn[2] for sv39.
static size_t vaddr_to_l0_index(vaddr_t addr) {
  return (addr >> (PAGE_SIZE_SHIFT + 2 * RISCV64_MMU_PT_SHIFT)) &
      (RISCV64_MMU_PT_ENTRIES - 1);
}

// Extract the level 1 physical page number from the virtual address.  In the
// RISC-V spec this corresponds to va.vpn[1] for sv39.
static size_t vaddr_to_l1_index(vaddr_t addr) {
  return (addr >> (PAGE_SIZE_SHIFT + RISCV64_MMU_PT_SHIFT)) &
      (RISCV64_MMU_PT_ENTRIES - 1);
}

// Extract the level 2 physical page number from the virtual address.  In the
// RISC-V spec this corresponds to va.vpn[0] for sv39.
static size_t vaddr_to_l2_index(vaddr_t addr) {
  return (addr >> PAGE_SIZE_SHIFT) & (RISCV64_MMU_PT_ENTRIES - 1);
}

// The following helper routines assume that code is running in physical
// addressing mode (mmu off). Any physical addresses calculated are assumed to
// be the same as virtual.  This function is guaranteed to return PAGE_SIZE
// aligned memory.
static paddr_t boot_alloc_ptable() {
  // Allocate a page out of the boot allocator, asking for a physical address.
  paddr_t ptr = boot_alloc_page_phys();
  memset(reinterpret_cast<char *>(ptr), 0, PAGE_SIZE);
  return ptr;
}

// Early boot time page table creation code, called from start.S while running
// in physical address space with the mmu disabled. This code should be position
// independent as long as it sticks to basic code.

// Called from start.S to configure the page tables to map the kernel
// wherever it is located physically to KERNEL_BASE.  This function should not
// call functions itself since the full ABI it not set up yet.
extern "C" zx_status_t riscv64_boot_map(pte_t* kernel_ptable0,
                                        vaddr_t vaddr,
                                        paddr_t paddr,
                                        size_t len,
                                        const pte_t flags) {
  vaddr &= RISCV64_MMU_CANONICAL_MASK;

  // Loop through the virtual range and map each physical page using the largest
  // page size supported. Allocates necessary page tables along the way.

  while (len > 0) {
    size_t index0 = vaddr_to_l0_index(vaddr);
    pte_t* kernel_ptable1 = nullptr;
    if (!RISCV64_PTE_IS_VALID(kernel_ptable0[index0])) {
      // A large page can be used if both the virtual and physical addresses
      // are aligned to the large page size and the remaining amount of memory
      // to map is at least the large page size.
/*      bool can_map_large_file =
          ((vaddr & l0_large_page_size_mask) == 0) &&
          ((paddr & l0_large_page_size_mask) == 0) &&
          len >= l0_large_page_size;
      if (can_map_large_file) {
        kernel_ptable0[index0] =
            RISCV64_PTE_PPN_TO_PTE((paddr & ~l0_large_page_size_mask)) | flags;
        vaddr += l0_large_page_size;
        paddr += l0_large_page_size;
        len -= l0_large_page_size;
        continue;
      } */

      paddr_t pa = boot_alloc_ptable();
      kernel_ptable0[index0] = RISCV64_PTE_PPN_TO_PTE(pa) | RISCV64_PTE_V;
      kernel_ptable1 = reinterpret_cast<pte_t*>(pa);
    } else if (!RISCV64_PTE_IS_LEAF(kernel_ptable0[index0])) {
      kernel_ptable1 = reinterpret_cast<pte_t*>(RISCV64_PTE_PPN(kernel_ptable0[index0]));
    } else {
      return ZX_ERR_BAD_STATE;
    }

    // Setup level 1 PTE.
    size_t index1 = vaddr_to_l1_index(vaddr);
    pte_t* kernel_ptable2 = nullptr;
    if (!RISCV64_PTE_IS_VALID(kernel_ptable1[index1])) {
      // A large page can be used if both the virtual and physical addresses
      // are aligned to the large page size and the remaining amount of memory
      // to map is at least the large page size.
      bool can_map_large_file =
          ((vaddr & l1_large_page_size_mask) == 0) &&
          ((paddr & l1_large_page_size_mask) == 0) &&
          len >= l1_large_page_size;
      if (can_map_large_file) {
        kernel_ptable1[index1] =
            RISCV64_PTE_PPN_TO_PTE((paddr & ~l1_large_page_size_mask)) | flags;
        vaddr += l1_large_page_size;
        paddr += l1_large_page_size;
        len -= l1_large_page_size;
        continue;
      }

      paddr_t pa = boot_alloc_ptable();
      kernel_ptable1[index1] = RISCV64_PTE_PPN_TO_PTE(pa) | RISCV64_PTE_V;
      kernel_ptable2 = reinterpret_cast<pte_t*>(pa);
    } else if (!RISCV64_PTE_IS_LEAF(kernel_ptable1[index1])) {
      kernel_ptable2 = reinterpret_cast<pte_t*>(RISCV64_PTE_PPN(kernel_ptable1[index1]));
    } else {
      return ZX_ERR_BAD_STATE;
    }

    // Setup level 2 PTE (always a leaf).
    size_t index2 = vaddr_to_l2_index(vaddr);
    kernel_ptable2[index2] = RISCV64_PTE_PPN_TO_PTE(paddr) | flags;

    vaddr += PAGE_SIZE;
    paddr += PAGE_SIZE;
    len -= PAGE_SIZE;
  }

  return ZX_OK;
}

