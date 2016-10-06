// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <err.h>
#include <arch/x86/mmu.h>
#include <arch/mmu.h>

static bool mmu_tests(void* context) {
    BEGIN_TEST;
#ifdef ARCH_X86_64
    unittest_printf("creating large un-aligned vm region, and unmap it without mapping, make sure no leak (MG-315)\n");
    {
        arch_aspace_t aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        status_t err = arch_mmu_init_aspace(&aspace, 1UL << 20, size, 0);
        EXPECT_EQ(err, NO_ERROR, "init aspace");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // We want our region to be misaligned by at least a page, and for
        // it to straddle the PDP.
        vaddr_t va = (1UL << PDP_SHIFT) - (1UL << PD_SHIFT) + 2 * PAGE_SIZE;
        // Make sure alloc_size is less than 1 PD page, to exercise the
        // non-terminal code path.
        static const size_t alloc_size = (1UL << PD_SHIFT) - PAGE_SIZE;

        // Map a single page to force the lower PDP of the target region
        // to be created
        err = arch_mmu_map(&aspace, va - 3 * PAGE_SIZE, 0, 1, arch_rw_flags);
        EXPECT_EQ(err, NO_ERROR, "map single page");

        // Map the last page of the region
        err = arch_mmu_map(&aspace, va + alloc_size - PAGE_SIZE, 0, 1, arch_rw_flags);
        EXPECT_EQ(err, NO_ERROR, "map last page");

        paddr_t pa;
        uint flags;
        err = arch_mmu_query(&aspace, va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, NO_ERROR, "last entry is mapped");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has only had its last page touched)
        err = arch_mmu_unmap(&aspace, va, alloc_size / PAGE_SIZE);
        EXPECT_EQ(err, NO_ERROR, "unmap unallocated region");

        err = arch_mmu_query(&aspace, va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, ERR_NOT_FOUND, "last entry is not mapped anymore");

        // Unmap the single page from earlier
        err = arch_mmu_unmap(&aspace, va - 3 * PAGE_SIZE, 1);
        EXPECT_EQ(err, NO_ERROR, "unmap single page");

        err = arch_mmu_destroy_aspace(&aspace);
        EXPECT_EQ(err, NO_ERROR, "destroy aspace");
    }

    unittest_printf("creating large un-aligned vm region, and unmap it without mapping (MG-315)\n");
    {
        arch_aspace_t aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        status_t err = arch_mmu_init_aspace(&aspace, 1UL << 20, size, 0);
        EXPECT_EQ(err, NO_ERROR, "init aspace");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // We want our region to be misaligned by a page, and for it to
        // straddle the PDP
        vaddr_t va = (1UL << PDP_SHIFT) - (1UL << PD_SHIFT) + PAGE_SIZE;
        // Make sure alloc_size is bigger than 1 PD page, to exercise the
        // non-terminal code path.
        static const size_t alloc_size = 3UL << PD_SHIFT;

        // Map a single page to force the lower PDP of the target region
        // to be created
        err = arch_mmu_map(&aspace, va - 2 * PAGE_SIZE, 0, 1, arch_rw_flags);
        EXPECT_EQ(err, NO_ERROR, "map single page");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has not been touched)
        err = arch_mmu_unmap(&aspace, va, alloc_size / PAGE_SIZE);
        EXPECT_EQ(err, NO_ERROR, "unmap unallocated region");

        // Unmap the single page from earlier
        err = arch_mmu_unmap(&aspace, va - 2 * PAGE_SIZE, 1);
        EXPECT_EQ(err, NO_ERROR, "unmap single page");

        err = arch_mmu_destroy_aspace(&aspace);
        EXPECT_EQ(err, NO_ERROR, "destroy aspace");
    }
#endif

    unittest_printf("done with mmu tests\n");
    END_TEST;
}

UNITTEST_START_TESTCASE(x86_mmu_tests)
UNITTEST("mmu tests", mmu_tests)
UNITTEST_END_TESTCASE(x86_mmu_tests, "x86_mmu", "x86 mmu tests", NULL, NULL);
