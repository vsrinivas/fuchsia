// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/aspace.h>
#include <arch/mmu.h>
#include <arch/x86/mmu.h>
#include <bits.h>
#include <err.h>
#include <lib/unittest/unittest.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>
#include <zircon/types.h>

static bool mmu_tests() {
    BEGIN_TEST;
    unittest_printf("creating large un-aligned vm region, and unmap it without mapping, make sure no leak (ZX-315)\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        zx_status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, ZX_OK, "init aspace");
        EXPECT_EQ(aspace.pt_pages(), 1u, "single page for PML4 table");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // We want our region to be misaligned by at least a page, and for
        // it to straddle the PDP.
        vaddr_t va = (1UL << PDP_SHIFT) - (1UL << PD_SHIFT) + 2 * PAGE_SIZE;
        // Make sure alloc_size is less than 1 PD page, to exercise the
        // non-terminal code path.
        static const size_t alloc_size = (1UL << PD_SHIFT) - PAGE_SIZE;

        // Map a single page to force the lower PDP of the target region
        // to be created
        size_t mapped;
        err = aspace.MapContiguous(va - 3 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map single page");
        EXPECT_EQ(mapped, 1u, "map single page");
        EXPECT_EQ(aspace.pt_pages(), 4u,
                  "map single page, PDP, PD and PT tables allocated");

        // Map the last page of the region
        err = aspace.MapContiguous(va + alloc_size - PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map last page");
        EXPECT_EQ(mapped, 1u, "map single page");
        EXPECT_EQ(aspace.pt_pages(), 6u,
                  "map single page, PD and PT tables allocated");

        paddr_t pa;
        uint flags;
        err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, ZX_OK, "last entry is mapped");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has only had its last page touched)
        size_t unmapped;
        err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap unallocated region");
        EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");
        EXPECT_EQ(aspace.pt_pages(), 4u, "unmap allocated region");

        err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, ZX_ERR_NOT_FOUND, "last entry is not mapped anymore");

        // Unmap the single page from earlier
        err = aspace.Unmap(va - 3 * PAGE_SIZE, 1, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap single page");
        EXPECT_EQ(unmapped, 1u, "unmap unallocated region");
        EXPECT_EQ(aspace.pt_pages(), 1u, "unmap single page");

        err = aspace.Destroy();
        EXPECT_EQ(err, ZX_OK, "destroy aspace");
    }

    unittest_printf("creating large un-aligned vm region, and unmap it without mapping (ZX-315)\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        zx_status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, ZX_OK, "init aspace");
        EXPECT_EQ(aspace.pt_pages(), 1u, "single page for PML4 table");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // We want our region to be misaligned by a page, and for it to
        // straddle the PDP
        vaddr_t va = (1UL << PDP_SHIFT) - (1UL << PD_SHIFT) + PAGE_SIZE;
        // Make sure alloc_size is bigger than 1 PD page, to exercise the
        // non-terminal code path.
        static const size_t alloc_size = 3UL << PD_SHIFT;

        // Map a single page to force the lower PDP of the target region
        // to be created
        size_t mapped;
        err = aspace.MapContiguous(va - 2 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map single page");
        EXPECT_EQ(mapped, 1u, "map single page");
        EXPECT_EQ(aspace.pt_pages(), 4u,
                  "map single page, PDP, PD and PT tables allocated");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has not been touched)
        size_t unmapped;
        err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap unallocated region");
        EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");
        EXPECT_EQ(aspace.pt_pages(), 4u, "unmap unallocated region");

        // Unmap the single page from earlier
        err = aspace.Unmap(va - 2 * PAGE_SIZE, 1, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap single page");
        EXPECT_EQ(unmapped, 1u, "unmap single page");
        EXPECT_EQ(aspace.pt_pages(), 1u, "unmap single page");

        err = aspace.Destroy();
        EXPECT_EQ(err, ZX_OK, "destroy aspace");
    }

    unittest_printf("creating large vm region, and change permissions\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        zx_status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, ZX_OK, "init aspace");
        EXPECT_EQ(aspace.pt_pages(), 1u, "single page for PML4 table");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        vaddr_t va = 1UL << PDP_SHIFT;
        // Force a large page.
        static const size_t alloc_size = 1UL << PD_SHIFT;

        size_t mapped;
        err = aspace.MapContiguous(va, 0, alloc_size / PAGE_SIZE, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map large page");
        EXPECT_EQ(mapped, 512u, "map large page");
        EXPECT_EQ(aspace.pt_pages(), 3u, "map large page");

        err = aspace.Protect(va + PAGE_SIZE, 1, ARCH_MMU_FLAG_PERM_READ);
        EXPECT_EQ(err, ZX_OK, "protect single page");
        EXPECT_EQ(aspace.pt_pages(), 4u,
                  "protect single page, split large page");

        err = aspace.Destroy();
        EXPECT_EQ(err, ZX_OK, "destroy aspace");
    }

    unittest_printf("done with mmu tests\n");
    END_TEST;
}

static bool check_virtual_address_mapped(uint64_t* pml4, vaddr_t va) {
    constexpr uint kPageTableLevels = 4;

    // Virtual Address is split at [47:39], [38:30], [29:21], [20:12]
    uint64_t offsets[kPageTableLevels] = {
        BITS_SHIFT(va, 47, 39),
        BITS_SHIFT(va, 38, 30),
        BITS_SHIFT(va, 29, 21),
        BITS_SHIFT(va, 20, 12)
    };
    uint64_t* current_level = pml4;
    for (uint i = 0; i < kPageTableLevels; i++) {
        uint64_t index = offsets[i];
        uint64_t pte = current_level[index];
        if ((pte & X86_MMU_PG_P) == 0) {
            return false;
        }

        uint64_t next_level_va = X86_PHYS_TO_VIRT(pte & X86_PG_FRAME);
        current_level = reinterpret_cast<uint64_t*>(next_level_va);
    }
    return true;
}

static bool x86_arch_vmaspace_usermmu_tests()
{
    BEGIN_TEST;

    constexpr uint kPtPerPageTable = PAGE_SIZE / sizeof(uint64_t);
    constexpr uint kUserPtPerPageTable = kPtPerPageTable / 2;
    {
        constexpr uintptr_t kTestVirtualAddress = 4ull * 1024 * 1024 * 1024 - PAGE_SIZE;
        // Basic test - make an aspace, map something, query it, check page tables, unmap
        X86ArchVmAspace aspace;
        EXPECT_EQ(ZX_OK, aspace.Init(0, 4ull * 1024 * 1024 * 1024, /*mmu_flags=*/0), "");
        uint64_t* const pml4 = reinterpret_cast<uint64_t*>(X86_PHYS_TO_VIRT(aspace.pt_phys()));
        // Expect no user mode mappings in an empty address space.
        for (uint i = 0; i < kUserPtPerPageTable; i++) {
            EXPECT_EQ(pml4[i], 0u, "");
        }

        paddr_t pa = 0;
        size_t mapped;
        vm_page_t* vm_page;
        pmm_alloc_page(/*alloc_flags=*/0, &vm_page, &pa);
        EXPECT_EQ(ZX_OK, aspace.Map(kTestVirtualAddress, &pa, 1,
                                    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, &mapped),
                  "");
        EXPECT_EQ(1u, mapped, "");
        // Directly examine page tables to ensure there's a mapping.
        EXPECT_EQ(check_virtual_address_mapped(pml4, kTestVirtualAddress), true, "");

        // Use query() interface to find a mapping.
        paddr_t retrieved_pa;
        uint flags;
        EXPECT_EQ(ZX_OK, aspace.Query(kTestVirtualAddress, &retrieved_pa, &flags), "");
        EXPECT_EQ(retrieved_pa, pa, "");

        size_t unmapped;
        EXPECT_EQ(ZX_OK, aspace.Unmap(kTestVirtualAddress, 1, &unmapped), "");
        EXPECT_EQ(unmapped, mapped, "");
        EXPECT_EQ(check_virtual_address_mapped(pml4, kTestVirtualAddress), false, "");
        // Expect no user mode mappings after the user mapping was removed.
        for (uint i = 0; i < kUserPtPerPageTable; i++) {
            EXPECT_EQ(pml4[i], 0u, "");
        }
        pmm_free_page(vm_page);

        aspace.Destroy();
    }

    END_TEST;
}

UNITTEST_START_TESTCASE(x86_mmu_tests)
UNITTEST("mmu tests", mmu_tests)
UNITTEST("user-aspace page table tests", x86_arch_vmaspace_usermmu_tests)
UNITTEST_END_TESTCASE(x86_mmu_tests, "x86_mmu", "x86 mmu tests");
