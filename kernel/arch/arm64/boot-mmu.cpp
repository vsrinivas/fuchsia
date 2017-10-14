// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/mmu.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <vm/bootalloc.h>

// Early boot time page table creation code, called from start.S while running in physical address space
// with the mmu disabled. This code should be position independent as long as it sticks to basic code.

// this code only works on a 4K page granule, 48 bits of kernel address space
static_assert(MMU_KERNEL_PAGE_SIZE_SHIFT == 12, "");
static_assert(MMU_KERNEL_SIZE_SHIFT == 48, "");

static size_t vaddr_to_l0_index(uintptr_t addr) {
    return (addr >> MMU_KERNEL_TOP_SHIFT) & (MMU_KERNEL_PAGE_TABLE_ENTRIES_TOP - 1);
}

static size_t vaddr_to_l1_index(uintptr_t addr) {
    return (addr >> MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 1)) & (MMU_KERNEL_PAGE_TABLE_ENTRIES - 1);
}

static size_t vaddr_to_l2_index(uintptr_t addr) {
    return (addr >> MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 2)) & (MMU_KERNEL_PAGE_TABLE_ENTRIES - 1);
}

static size_t vaddr_to_l3_index(uintptr_t addr) {
    return (addr >> MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 3)) & (MMU_KERNEL_PAGE_TABLE_ENTRIES - 1);
}

// called from start.S to grab another page to back a page table from the boot allocator
__NO_SAFESTACK
extern "C" pte_t* boot_alloc_ptable() {
    // allocate a page out of the boot allocator, asking for a physical address
    pte_t* ptr = reinterpret_cast<pte_t*>(boot_alloc_page_phys());

    // avoid using memset, since this relies on dc zva instruction, which isn't set up at
    // this point in the boot process
    // use a volatile pointer to make sure
    volatile pte_t* vptr = ptr;
    for (auto i = 0; i < MMU_KERNEL_PAGE_TABLE_ENTRIES; i++)
        vptr[i] = 0;

    return ptr;
}

// called from start.S to configure level 1-3 page tables to map the kernel wherever it is located physically
// to KERNEL_BASE
__NO_SAFESTACK
extern "C" void arm64_boot_map(pte_t* kernel_table0,
                               const vaddr_t vaddr,
                               const paddr_t paddr,
                               const size_t len) {

    // loop through the virtual range and map each consecutive physical page using simple 4K mappings, allocating
    // necessary page tables along the way
    size_t off = 0;
    while (off < len) {
        // make sure the level 1 pointer is valid
        size_t index0 = vaddr_to_l0_index(vaddr + off);
        pte_t* kernel_table1 = nullptr;
        if ((kernel_table0[index0] & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_DESCRIPTOR_INVALID) {
            kernel_table1 = boot_alloc_ptable();

            kernel_table0[index0] = (reinterpret_cast<pte_t>(kernel_table1) & MMU_PTE_OUTPUT_ADDR_MASK) |
                                    MMU_PTE_L012_DESCRIPTOR_TABLE;
        }
        kernel_table1 = reinterpret_cast<pte_t*>(kernel_table0[index0] & MMU_PTE_OUTPUT_ADDR_MASK);

        // make sure the level 2 pointer is valid
        size_t index1 = vaddr_to_l1_index(vaddr + off);
        pte_t* kernel_table2 = nullptr;
        if ((kernel_table1[index1] & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_DESCRIPTOR_INVALID) {
            kernel_table2 = boot_alloc_ptable();

            kernel_table1[index1] = (reinterpret_cast<pte_t>(kernel_table2) & MMU_PTE_OUTPUT_ADDR_MASK) |
                                    MMU_PTE_L012_DESCRIPTOR_TABLE;
        }
        kernel_table2 = reinterpret_cast<pte_t*>(kernel_table1[index1] & MMU_PTE_OUTPUT_ADDR_MASK);

        // make sure the level 3 pointer is valid
        size_t index2 = vaddr_to_l2_index(vaddr + off);
        pte_t* kernel_table3 = nullptr;
        if ((kernel_table2[index2] & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_DESCRIPTOR_INVALID) {
            kernel_table3 = boot_alloc_ptable();

            kernel_table2[index2] = (reinterpret_cast<pte_t>(kernel_table3) & MMU_PTE_OUTPUT_ADDR_MASK) |
                                    MMU_PTE_L012_DESCRIPTOR_TABLE;
        }
        kernel_table3 = reinterpret_cast<pte_t*>(kernel_table2[index2] & MMU_PTE_OUTPUT_ADDR_MASK);

        // generate a standard page mapping
        // vm initialization will lock these pages down further later in the boot process
        // TODO: use larger and/or combined pages if the alignment/size lines up
        uint64_t flags = MMU_PTE_L3_DESCRIPTOR_PAGE |
                         MMU_PTE_ATTR_AF |
                         MMU_PTE_ATTR_SH_INNER_SHAREABLE |
                         MMU_PTE_ATTR_NORMAL_MEMORY |
                         MMU_PTE_ATTR_AP_P_RW_U_NA;

        size_t index3 = vaddr_to_l3_index(vaddr + off);
        kernel_table3[index3] = ((paddr + off) & MMU_PTE_OUTPUT_ADDR_MASK) | flags;

        off += PAGE_SIZE;
    }
}
