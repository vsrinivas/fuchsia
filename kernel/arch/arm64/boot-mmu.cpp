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
#include <vm/physmap.h>

// Early boot time page table creation code, called from start.S while running in physical address space
// with the mmu disabled. This code should be position independent as long as it sticks to basic code.

// this code only works on a 4K page granule, 48 bits of kernel address space
static_assert(MMU_KERNEL_PAGE_SIZE_SHIFT == 12, "");
static_assert(MMU_KERNEL_SIZE_SHIFT == 48, "");

// 1GB pages
const uintptr_t l1_large_page_size = 1UL << MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 1);
const uintptr_t l1_large_page_size_mask = l1_large_page_size - 1;

// 2MB pages
const uintptr_t l2_large_page_size = 1UL << MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 2);
const uintptr_t l2_large_page_size_mask = l2_large_page_size - 2;

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

// inner mapping routine passed two helper routines
__NO_SAFESTACK
static inline zx_status_t _arm64_boot_map(pte_t* kernel_table0,
                                          const vaddr_t vaddr,
                                          const paddr_t paddr,
                                          const size_t len,
                                          const pte_t flags,
                                          paddr_t (*alloc_func)(),
                                          pte_t* phys_to_virt(paddr_t)) {

    // loop through the virtual range and map each physical page, using the largest
    // page size supported. Allocates necessar page tables along the way.
    size_t off = 0;
    while (off < len) {
        // make sure the level 1 pointer is valid
        size_t index0 = vaddr_to_l0_index(vaddr + off);
        pte_t* kernel_table1 = nullptr;
        switch (kernel_table0[index0] & MMU_PTE_DESCRIPTOR_MASK) {
        default: { // invalid/unused entry
            paddr_t pa = alloc_func();

            kernel_table0[index0] = (pa & MMU_PTE_OUTPUT_ADDR_MASK) |
                                    MMU_PTE_L012_DESCRIPTOR_TABLE;
            // fallthrough
        }
        case MMU_PTE_L012_DESCRIPTOR_TABLE:
            kernel_table1 = phys_to_virt(kernel_table0[index0] & MMU_PTE_OUTPUT_ADDR_MASK);
            break;
        case MMU_PTE_L012_DESCRIPTOR_BLOCK:
            // not legal to have a block pointer at this level
            return ZX_ERR_BAD_STATE;
        }

        // make sure the level 2 pointer is valid
        size_t index1 = vaddr_to_l1_index(vaddr + off);
        pte_t* kernel_table2 = nullptr;
        switch (kernel_table1[index1] & MMU_PTE_DESCRIPTOR_MASK) {
        default: { // invalid/unused entry
            // a large page at this level is 1GB long, see if we can make one here
            if ((((vaddr + off) & l1_large_page_size_mask) == 0) &&
                (((paddr + off) & l1_large_page_size_mask) == 0) &&
                (len - off) >= l1_large_page_size) {

                // set up a 1GB page here
                kernel_table1[index1] = ((paddr + off) & ~l1_large_page_size_mask) |
                                        flags | MMU_PTE_L012_DESCRIPTOR_BLOCK;

                off += l1_large_page_size;
                continue;
            }

            paddr_t pa = alloc_func();

            kernel_table1[index1] = (pa & MMU_PTE_OUTPUT_ADDR_MASK) |
                                    MMU_PTE_L012_DESCRIPTOR_TABLE;
            // fallthrough
        }
        case MMU_PTE_L012_DESCRIPTOR_TABLE:
            kernel_table2 = phys_to_virt(kernel_table1[index1] & MMU_PTE_OUTPUT_ADDR_MASK);
            break;
        case MMU_PTE_L012_DESCRIPTOR_BLOCK:
            // not legal to have a block pointer at this level
            return ZX_ERR_BAD_STATE;
        }

        // make sure the level 3 pointer is valid
        size_t index2 = vaddr_to_l2_index(vaddr + off);
        pte_t* kernel_table3 = nullptr;
        switch (kernel_table2[index2] & MMU_PTE_DESCRIPTOR_MASK) {
        default: { // invalid/unused entry
            // a large page at this level is 2MB long, see if we can make one here
            if ((((vaddr + off) & l2_large_page_size_mask) == 0) &&
                (((paddr + off) & l2_large_page_size_mask) == 0) &&
                (len - off) >= l2_large_page_size) {

                // set up a 2MB page here
                kernel_table2[index2] = ((paddr + off) & ~l2_large_page_size_mask) |
                                        flags | MMU_PTE_L012_DESCRIPTOR_BLOCK;

                off += l2_large_page_size;
                continue;
            }

            paddr_t pa = alloc_func();

            kernel_table2[index2] = (pa & MMU_PTE_OUTPUT_ADDR_MASK) |
                                    MMU_PTE_L012_DESCRIPTOR_TABLE;
            // fallthrough
        }
        case MMU_PTE_L012_DESCRIPTOR_TABLE:
            kernel_table3 = phys_to_virt(kernel_table2[index2] & MMU_PTE_OUTPUT_ADDR_MASK);
            break;
        case MMU_PTE_L012_DESCRIPTOR_BLOCK:
            // not legal to have a block pointer at this level
            return ZX_ERR_BAD_STATE;
        }

        // generate a standard page mapping
        size_t index3 = vaddr_to_l3_index(vaddr + off);
        kernel_table3[index3] = ((paddr + off) & MMU_PTE_OUTPUT_ADDR_MASK) | flags | MMU_PTE_L3_DESCRIPTOR_PAGE;

        off += PAGE_SIZE;
    }

    return ZX_OK;
}

// called from start.S to configure level 1-3 page tables to map the kernel wherever it is located physically
// to KERNEL_BASE
__NO_SAFESTACK
extern "C" zx_status_t arm64_boot_map(pte_t* kernel_table0,
                                      const vaddr_t vaddr,
                                      const paddr_t paddr,
                                      const size_t len,
                                      const pte_t flags) {

    // the following helper routines assume that code is running in physical addressing mode (mmu off).
    // any physical addresses calculated are assumed to be the same as virtual
    auto alloc = []() -> paddr_t {
        // allocate a page out of the boot allocator, asking for a physical address
        paddr_t pa = boot_alloc_page_phys();

        // avoid using memset, since this relies on dc zva instruction, which isn't set up at
        // this point in the boot process
        // use a volatile pointer to make sure the compiler doesn't emit a memset call
        volatile pte_t* vptr = reinterpret_cast<volatile pte_t*>(pa);
        for (auto i = 0; i < MMU_KERNEL_PAGE_TABLE_ENTRIES; i++)
            vptr[i] = 0;

        return pa;
    };

    auto phys_to_virt = [](paddr_t pa) -> pte_t* {
        return reinterpret_cast<pte_t*>(pa);
    };

    return _arm64_boot_map(kernel_table0, vaddr, paddr, len, flags, alloc, phys_to_virt);
}

// called a bit later in the boot process once the kernel is in virtual memory to map early kernel data
extern "C" zx_status_t arm64_boot_map_v(const vaddr_t vaddr,
                                        const paddr_t paddr,
                                        const size_t len,
                                        const pte_t flags) {

    // assumed to be running with virtual memory enabled, so use a slightly different set of routines
    // to allocate and find the virtual mapping of memory
    auto alloc = []() -> paddr_t {
        // allocate a page out of the boot allocator, asking for a physical address
        paddr_t pa = boot_alloc_page_phys();

        // zero the memory using the physmap
        void* ptr = paddr_to_physmap(pa);
        memset(ptr, 0, MMU_KERNEL_PAGE_TABLE_ENTRIES * sizeof(pte_t));

        return pa;
    };

    auto phys_to_virt = [](paddr_t pa) -> pte_t* {
        return reinterpret_cast<pte_t*>(paddr_to_physmap(pa));
    };

    return _arm64_boot_map(arm64_get_kernel_ptable(), vaddr, paddr, len, flags, alloc, phys_to_virt);
}
