// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <err.h>
#include <arch/aspace.h>
#include <arch/mmu.h>
#include <arch/x86/mmu.h>
#include <vm/arch_vm_aspace.h>

static bool mmu_tests(void* context) {
    BEGIN_TEST;
    unittest_printf("creating large un-aligned vm region, and unmap it without mapping, make sure no leak (MG-315)\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, MX_OK, "init aspace");
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
        err = aspace.Map(va - 3 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, MX_OK, "map single page");
        EXPECT_EQ(mapped, 1u, "map single page");
        EXPECT_EQ(aspace.pt_pages(), 4u,
                  "map single page, PDP, PD and PT tables allocated");

        // Map the last page of the region
        err = aspace.Map(va + alloc_size - PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, MX_OK, "map last page");
        EXPECT_EQ(mapped, 1u, "map single page");
        EXPECT_EQ(aspace.pt_pages(), 6u,
                  "map single page, PD and PT tables allocated");

        paddr_t pa;
        uint flags;
        err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, MX_OK, "last entry is mapped");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has only had its last page touched)
        size_t unmapped;
        err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
        EXPECT_EQ(err, MX_OK, "unmap unallocated region");
        EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");
        EXPECT_EQ(aspace.pt_pages(), 4u, "unmap allocated region");

        err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, MX_ERR_NOT_FOUND, "last entry is not mapped anymore");

        // Unmap the single page from earlier
        err = aspace.Unmap(va - 3 * PAGE_SIZE, 1, &unmapped);
        EXPECT_EQ(err, MX_OK, "unmap single page");
        EXPECT_EQ(unmapped, 1u, "unmap unallocated region");
        EXPECT_EQ(aspace.pt_pages(), 1u, "unmap single page");

        err = aspace.Destroy();
        EXPECT_EQ(err, MX_OK, "destroy aspace");
    }

    unittest_printf("creating large un-aligned vm region, and unmap it without mapping (MG-315)\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, MX_OK, "init aspace");
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
        err = aspace.Map(va - 2 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, MX_OK, "map single page");
        EXPECT_EQ(mapped, 1u, "map single page");
        EXPECT_EQ(aspace.pt_pages(), 4u,
                  "map single page, PDP, PD and PT tables allocated");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has not been touched)
        size_t unmapped;
        err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
        EXPECT_EQ(err, MX_OK, "unmap unallocated region");
        EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");
        EXPECT_EQ(aspace.pt_pages(), 4u, "unmap unallocated region");

        // Unmap the single page from earlier
        err = aspace.Unmap(va - 2 * PAGE_SIZE, 1, &unmapped);
        EXPECT_EQ(err, MX_OK, "unmap single page");
        EXPECT_EQ(unmapped, 1u, "unmap single page");
        EXPECT_EQ(aspace.pt_pages(), 1u, "unmap single page");

        err = aspace.Destroy();
        EXPECT_EQ(err, MX_OK, "destroy aspace");
    }

    unittest_printf("creating large vm region, and change permissions\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, MX_OK, "init aspace");
        EXPECT_EQ(aspace.pt_pages(), 1u, "single page for PML4 table");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        vaddr_t va = 1UL << PDP_SHIFT;
        // Force a large page.
        static const size_t alloc_size = 1UL << PD_SHIFT;

        size_t mapped;
        err = aspace.Map(va, 0, alloc_size / PAGE_SIZE, arch_rw_flags, &mapped);
        EXPECT_EQ(err, MX_OK, "map large page");
        EXPECT_EQ(mapped, 512u, "map large page");
        EXPECT_EQ(aspace.pt_pages(), 3u, "map large page");

        err = aspace.Protect(va + PAGE_SIZE, 1, ARCH_MMU_FLAG_PERM_READ);
        EXPECT_EQ(err, MX_OK, "protect single page");
        EXPECT_EQ(aspace.pt_pages(), 4u,
                  "protect single page, split large page");

        err = aspace.Destroy();
        EXPECT_EQ(err, MX_OK, "destroy aspace");
    }

    unittest_printf("done with mmu tests\n");
    END_TEST;
}

UNITTEST_START_TESTCASE(x86_mmu_tests)
UNITTEST("mmu tests", mmu_tests)
UNITTEST_END_TESTCASE(x86_mmu_tests, "x86_mmu", "x86 mmu tests", nullptr, nullptr);
