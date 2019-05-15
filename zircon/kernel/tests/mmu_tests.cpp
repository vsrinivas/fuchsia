// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/aspace.h>
#include <arch/mmu.h>
#include <bits.h>
#include <err.h>
#include <lib/unittest/unittest.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>
#include <zircon/types.h>

#ifdef __x86_64__
#include <arch/x86/mmu.h>
#define PGTABLE_L1_SHIFT    PDP_SHIFT
#define PGTABLE_L2_SHIFT    PD_SHIFT
#else
#define PGTABLE_L1_SHIFT    MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 1)
#define PGTABLE_L2_SHIFT    MMU_LX_X(MMU_KERNEL_PAGE_SIZE_SHIFT, 2)
#endif

static bool mmu_tests() {
    BEGIN_TEST;
    unittest_printf("creating large un-aligned vm region, and unmap it without mapping, make sure no leak (ZX-315)\n");
    {
        ArchVmAspace aspace;
        vaddr_t base = 1UL << 20;
        size_t size = (1UL << 47) - base - (1UL << 20);
        zx_status_t err = aspace.Init(1UL << 20, size, 0);
        EXPECT_EQ(err, ZX_OK, "init aspace");

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // We want our region to be misaligned by at least a page, and for
        // it to straddle the PDP.
        vaddr_t va = (1UL << PGTABLE_L1_SHIFT) - (1UL << PGTABLE_L2_SHIFT) + 2 * PAGE_SIZE;
        // Make sure alloc_size is less than 1 PD page, to exercise the
        // non-terminal code path.
        static const size_t alloc_size = (1UL << PGTABLE_L2_SHIFT) - PAGE_SIZE;

        // Map a single page to force the lower PDP of the target region
        // to be created
        size_t mapped;
        err = aspace.MapContiguous(va - 3 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map single page");
        EXPECT_EQ(mapped, 1u, "map single page");

        // Map the last page of the region
        err = aspace.MapContiguous(va + alloc_size - PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map last page");
        EXPECT_EQ(mapped, 1u, "map single page");

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

        err = aspace.Query(va + alloc_size - PAGE_SIZE, &pa, &flags);
        EXPECT_EQ(err, ZX_ERR_NOT_FOUND, "last entry is not mapped anymore");

        // Unmap the single page from earlier
        err = aspace.Unmap(va - 3 * PAGE_SIZE, 1, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap single page");
        EXPECT_EQ(unmapped, 1u, "unmap unallocated region");

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

        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // We want our region to be misaligned by a page, and for it to
        // straddle the PDP
        vaddr_t va = (1UL << PGTABLE_L1_SHIFT) - (1UL << PGTABLE_L2_SHIFT) + PAGE_SIZE;
        // Make sure alloc_size is bigger than 1 PD page, to exercise the
        // non-terminal code path.
        static const size_t alloc_size = 3UL << PGTABLE_L2_SHIFT;

        // Map a single page to force the lower PDP of the target region
        // to be created
        size_t mapped;
        err = aspace.MapContiguous(va - 2 * PAGE_SIZE, 0, 1, arch_rw_flags, &mapped);
        EXPECT_EQ(err, ZX_OK, "map single page");
        EXPECT_EQ(mapped, 1u, "map single page");

        // Attempt to unmap the target region (analogous to unmapping a demand
        // paged region that has not been touched)
        size_t unmapped;
        err = aspace.Unmap(va, alloc_size / PAGE_SIZE, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap unallocated region");
        EXPECT_EQ(unmapped, alloc_size / PAGE_SIZE, "unmap unallocated region");

        // Unmap the single page from earlier
        err = aspace.Unmap(va - 2 * PAGE_SIZE, 1, &unmapped);
        EXPECT_EQ(err, ZX_OK, "unmap single page");
        EXPECT_EQ(unmapped, 1u, "unmap single page");

        err = aspace.Destroy();
        EXPECT_EQ(err, ZX_OK, "destroy aspace");
    }

    END_TEST;
}

UNITTEST_START_TESTCASE(mmu_tests)
UNITTEST("mmu tests", mmu_tests)
UNITTEST_END_TESTCASE(mmu_tests, "mmu", "mmu tests");
