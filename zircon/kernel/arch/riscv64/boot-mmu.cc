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

// 1GB pages
const uintptr_t l1_large_page_size = 1UL << (PAGE_SIZE_SHIFT + 2 * RISCV64_MMU_PT_SHIFT);
const uintptr_t l1_large_page_size_mask = l1_large_page_size - 1;

// 2MB pages
const uintptr_t l2_large_page_size = 1UL << (PAGE_SIZE_SHIFT + RISCV64_MMU_PT_SHIFT);
const uintptr_t l2_large_page_size_mask = l2_large_page_size - 1;

static size_t vaddr_to_l0_index(uintptr_t addr) {
  // canonicalize the address
  addr &= RISCV64_MMU_CANONICAL_MASK;
  return ((addr >> PAGE_SIZE_SHIFT) >> (3 * RISCV64_MMU_PT_SHIFT)) & (RISCV64_MMU_PT_ENTRIES - 1);
}

static size_t vaddr_to_l1_index(uintptr_t addr) {
  // canonicalize the address
  addr &= RISCV64_MMU_CANONICAL_MASK;
  return ((addr >> PAGE_SIZE_SHIFT) >> (2 * RISCV64_MMU_PT_SHIFT)) & (RISCV64_MMU_PT_ENTRIES - 1);
}

static size_t vaddr_to_l2_index(uintptr_t addr) {
  // canonicalize the address
  addr &= RISCV64_MMU_CANONICAL_MASK;
  return ((addr >> PAGE_SIZE_SHIFT) >> RISCV64_MMU_PT_SHIFT) & (RISCV64_MMU_PT_ENTRIES - 1);
}

static size_t vaddr_to_l3_index(uintptr_t addr) {
  // canonicalize the address
  addr &= RISCV64_MMU_CANONICAL_MASK;
  return (addr >> PAGE_SIZE_SHIFT) & (RISCV64_MMU_PT_ENTRIES - 1);
}

// the following helper routines assume that code is running in physical addressing mode (mmu
// off). any physical addresses calculated are assumed to be the same as virtual
extern "C" paddr_t boot_alloc_ptable() {
  // allocate a page out of the boot allocator, asking for a physical address
  paddr_t ptr = boot_alloc_page_phys();
  memset(reinterpret_cast<char *>(ptr), 0, PAGE_SIZE);
  return ptr;
}

// Early boot time page table creation code, called from start.S while running in physical address
// space with the mmu disabled. This code should be position independent as long as it sticks to
// basic code.

// called from start.S to configure level 1-3 page tables to map the kernel wherever it is located
// physically to KERNEL_BASE
__NO_SAFESTACK
extern "C" /*zx_status_t*/ size_t riscv64_boot_map(pte_t* kernel_table0, const vaddr_t vaddr,
                                      const paddr_t paddr, const size_t len, const pte_t flags) {
  // loop through the virtual range and map each physical page, using the largest
  // page size supported. Allocates necessary page tables along the way.
  size_t off = 0;
  while (off < len) {
    // make sure the level 1 pointer is valid
    size_t index0 = vaddr_to_l0_index(vaddr + off);
    pte_t* kernel_table1 = nullptr;
    if (!(kernel_table0[index0] & RISCV64_PTE_V)) { // invalid/unused entry
      paddr_t pa = boot_alloc_ptable();
      kernel_table0[index0] = RISCV64_PTE_PPN_TO_PTE(pa) | RISCV64_PTE_V;
      kernel_table1 = reinterpret_cast<pte_t*>(pa);
    } else {
      if (!(kernel_table0[index0] & RISCV64_PTE_PERM_MASK)) {
        kernel_table1 = reinterpret_cast<pte_t*>(RISCV64_PTE_PPN(kernel_table0[index0]));
      } else {
        // not legal to have a block pointer at this level
        return ZX_ERR_BAD_STATE;
      }
    }

    // make sure the level 2 pointer is valid
    size_t index1 = vaddr_to_l1_index(vaddr + off);
    pte_t* kernel_table2 = nullptr;
    if (!(kernel_table1[index1] & RISCV64_PTE_V)) { // invalid/unused entry
      if ((((vaddr + off) & l1_large_page_size_mask) == 0) &&
          (((paddr + off) & l1_large_page_size_mask) == 0) && (len - off) >= l1_large_page_size) {
        kernel_table1[index1] = RISCV64_PTE_PPN_TO_PTE(((paddr + off) & ~l1_large_page_size_mask)) | flags;

        off += l1_large_page_size;
        continue;
      }

      paddr_t pa = boot_alloc_ptable();
      kernel_table1[index1] = RISCV64_PTE_PPN_TO_PTE(pa) | RISCV64_PTE_V;
      kernel_table2 = reinterpret_cast<pte_t*>(pa);
    } else {
      if (!(kernel_table1[index1] & RISCV64_PTE_PERM_MASK)) {
        kernel_table2 = reinterpret_cast<pte_t*>(RISCV64_PTE_PPN(kernel_table1[index1]));
      } else {
        // not legal to have a block pointer at this level
        return ZX_ERR_BAD_STATE;
      }
    }

    // make sure the level 3 pointer is valid
    size_t index2 = vaddr_to_l2_index(vaddr + off);
    pte_t* kernel_table3 = nullptr;
    if (!(kernel_table2[index2] & RISCV64_PTE_V)) { // invalid/unused entry
      if ((((vaddr + off) & l2_large_page_size_mask) == 0) &&
          (((paddr + off) & l2_large_page_size_mask) == 0) && (len - off) >= l2_large_page_size) {
        kernel_table2[index2] = RISCV64_PTE_PPN_TO_PTE(((paddr + off) & ~l2_large_page_size_mask)) | flags;

        off += l2_large_page_size;
        continue;
      }

      paddr_t pa = boot_alloc_ptable();
      kernel_table2[index2] = RISCV64_PTE_PPN_TO_PTE(pa) | RISCV64_PTE_V;
      kernel_table3 = reinterpret_cast<pte_t*>(pa);
    } else {
      if (!(kernel_table2[index2] & RISCV64_PTE_PERM_MASK)) {
        kernel_table3 = reinterpret_cast<pte_t*>(RISCV64_PTE_PPN(kernel_table2[index2]));
      } else {
        // not legal to have a block pointer at this level
        return ZX_ERR_BAD_STATE;
      }
    }

    // generate a standard page mapping
    size_t index3 = vaddr_to_l3_index(vaddr + off);
    kernel_table3[index3] = RISCV64_PTE_PPN_TO_PTE(paddr + off) | flags;

    off += PAGE_SIZE;
  }

  return ZX_OK;
}

