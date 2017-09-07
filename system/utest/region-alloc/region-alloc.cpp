// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <region-alloc/region-alloc.h>
#include <stdio.h>
#include <unittest/unittest.h>

#include "common.h"

namespace {

static bool ralloc_region_pools_test() {
    BEGIN_TEST;

    // Create a default constructed allocator on the stack.
    RegionAllocator alloc;

    {
        // Make sure that it refuses to perform any operations because it has no
        // RegionPool assigned to it yet.
        RegionAllocator::Region::UPtr tmp;
        EXPECT_EQ(MX_ERR_BAD_STATE, alloc.AddRegion({ 0u, 1u }));
        EXPECT_EQ(MX_ERR_BAD_STATE, alloc.GetRegion(1, tmp));
        EXPECT_EQ(MX_ERR_BAD_STATE, alloc.GetRegion({ 0u, 1u }, tmp));
        EXPECT_NULL(alloc.GetRegion(1));
        EXPECT_NULL(alloc.GetRegion({ 0u, 1u }));
    }

    // Make a region pool to manage bookkeeping allocations.
    auto pool = RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE);
    ASSERT_NONNULL(pool);

    // Assign our pool to our allocator, but hold onto the pool for now.
    ASSERT_EQ(MX_OK, alloc.SetRegionPool(pool));
    EXPECT_NONNULL(pool);

    // Create another allocator and transfer ownership of our region pool
    // reference to it.  Then let the allocator go out of scope.
    {
        RegionAllocator alloc2(fbl::move(pool));
        EXPECT_NULL(pool);
    }
    EXPECT_NULL(pool);

    // Add some regions to our allocator.
    for (size_t i = 0; i < fbl::count_of(GOOD_REGIONS); ++i)
        EXPECT_EQ(MX_OK, alloc.AddRegion(GOOD_REGIONS[i]));

    // Make a new pool and try to assign it to the allocator.  This should fail
    // because the allocator is currently using resources from its currently
    // assigned pool.
    auto pool2 = RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE);
    ASSERT_NONNULL(pool2);
    EXPECT_EQ(MX_ERR_BAD_STATE, alloc.SetRegionPool(pool2));

    // Add a bunch of adjacent regions to our pool.  Try to add so many
    // that we would normally run out of bookkeeping space.  We should not
    // actually run out, however, because the regions should get merged as they
    // get added.
    {
        ralloc_region_t tmp = { .base = GOOD_MERGE_REGION_BASE,
                                .size = GOOD_MERGE_REGION_SIZE };
        for (size_t i = 0; i < OOM_RANGE_LIMIT; ++i) {
            ASSERT_EQ(MX_OK, alloc.AddRegion(tmp));
            tmp.base += tmp.size;
        }
    }

    // Attempt (and fail) to add some bad regions (regions which overlap,
    // regions which wrap the address space)
    for (size_t i = 0; i < fbl::count_of(BAD_REGIONS); ++i)
        EXPECT_EQ(MX_ERR_INVALID_ARGS, alloc.AddRegion(BAD_REGIONS[i]));

    // Force the region bookkeeping pool to run out of memory by adding more and
    // more regions until we eventually run out of room.  Make sure that the
    // regions are not adjacent, or the internal bookkeeping will just merge
    // them.
    {
        size_t i;
        ralloc_region_t tmp = { .base = BAD_MERGE_REGION_BASE,
                                .size = BAD_MERGE_REGION_SIZE };
        for (i = 0; i < OOM_RANGE_LIMIT; ++i) {
            mx_status_t res;

            res = alloc.AddRegion(tmp);
            if (res != MX_OK) {
                EXPECT_EQ(MX_ERR_NO_MEMORY, res);
                break;
            }

            tmp.base += tmp.size + 1;
        }

        EXPECT_LT(i, OOM_RANGE_LIMIT);
    }

    // Reset allocator.  All of the existing available regions we had previously
    // added will be returned to the pool.
    alloc.Reset();

    // Now assign pool2 to the allocator.  Now that it is no longer using any
    // resources, this should succeed.
    EXPECT_EQ(MX_OK, alloc.SetRegionPool(fbl::move(pool2)));
    EXPECT_NULL(pool2);

    END_TEST;
}

static bool ralloc_by_size_test() {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.  Then add the test regions to it.
    RegionAllocator alloc(RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE));

    for (size_t i = 0; i < fbl::count_of(ALLOC_BY_SIZE_REGIONS); ++i)
        ASSERT_EQ(MX_OK, alloc.AddRegion(ALLOC_BY_SIZE_REGIONS[i]));

    // Run the alloc by size tests.  Hold onto the regions it allocates so they
    // don't automatically get returned to the pool.
    RegionAllocator::Region::UPtr regions[fbl::count_of(ALLOC_BY_SIZE_TESTS)];

    for (size_t i = 0; i < fbl::count_of(ALLOC_BY_SIZE_TESTS); ++i) {
        const alloc_by_size_alloc_test_t* TEST = ALLOC_BY_SIZE_TESTS + i;
        mx_status_t res = alloc.GetRegion(TEST->size, TEST->align, regions[i]);

        // Make sure we get the test result we were expecting.
        EXPECT_EQ(TEST->res, res);

        // If the allocation claimed to succeed, we should have gotten
        // back a non-null region.  Otherwise, we should have gotten a
        // null region back.
        if (res == MX_OK) {
            ASSERT_NONNULL(regions[i]);
        } else {
            EXPECT_NULL(regions[i]);
        }

        // If the allocation succeeded, and we expected it to succeed,
        // the allocation should have come from the test region we
        // expect and be aligned in the way we asked.
        if ((res == MX_OK) && (TEST->res == MX_OK)) {
            ASSERT_LT(TEST->region, fbl::count_of(ALLOC_BY_SIZE_TESTS));
            EXPECT_TRUE(region_contains_region(ALLOC_BY_SIZE_REGIONS + TEST->region,
                                               regions[i].get()));
            EXPECT_EQ(0u, regions[i]->base & (TEST->align - 1));
        }

    }

    // No need for any explicit cleanup.  Our region references will go out of
    // scope first and be returned to the allocator.  Then the allocator will
    // clean up, and release its bookkeeping pool reference in the process.

    END_TEST;
}

static bool ralloc_specific_test() {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.  Then add the test regions to it.
    RegionAllocator alloc(RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE));

    for (size_t i = 0; i < fbl::count_of(ALLOC_SPECIFIC_REGIONS); ++i)
        ASSERT_EQ(MX_OK, alloc.AddRegion(ALLOC_SPECIFIC_REGIONS[i]));

    // Run the alloc specific tests.  Hold onto the regions it allocates so they
    // don't automatically get returned to the pool.
    RegionAllocator::Region::UPtr regions[fbl::count_of(ALLOC_SPECIFIC_TESTS)];

    for (size_t i = 0; i < fbl::count_of(ALLOC_SPECIFIC_TESTS); ++i) {
        const alloc_specific_alloc_test_t* TEST = ALLOC_SPECIFIC_TESTS + i;
        mx_status_t res = alloc.GetRegion(TEST->req, regions[i]);

        // Make sure we get the test result we were expecting.
        EXPECT_EQ(TEST->res, res);

        // If the allocation claimed to succeed, we should have gotten back a
        // non-null region which exactly matches our requested region.
        if (res == MX_OK) {
            ASSERT_NONNULL(regions[i]);
            EXPECT_EQ(TEST->req.base, regions[i]->base);
            EXPECT_EQ(TEST->req.size, regions[i]->size);
        } else {
            EXPECT_NULL(regions[i]);
        }
    }

    // No need for any explicit cleanup.  Our region references will go out of
    // scope first and be returned to the allocator.  Then the allocator will
    // clean up, and release its bookkeeping pool reference in the process.

    END_TEST;
}

static bool ralloc_add_overlap_test() {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.  Then add the test regions to it.
    RegionAllocator alloc(RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE));

    // Add each of the regions specified by the test and check the expected results.
    for (size_t i = 0; i < fbl::count_of(ADD_OVERLAP_TESTS); ++i) {
        const alloc_add_overlap_test_t* TEST = ADD_OVERLAP_TESTS + i;

        mx_status_t res = alloc.AddRegion(TEST->reg, TEST->ovl);

        EXPECT_EQ(TEST->res, res);
        EXPECT_EQ(TEST->cnt, alloc.AvailableRegionCount());
    }

    END_TEST;
}

static bool ralloc_subtract_test() {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.  Then add the test regions to it.
    RegionAllocator alloc(RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE));

    // Run the test sequence, adding and subtracting regions and verifying the results.
    for (size_t i = 0; i < fbl::count_of(SUBTRACT_TESTS); ++i) {
        const alloc_subtract_test_t* TEST = SUBTRACT_TESTS + i;

        mx_status_t res;
        if (TEST->add)
            res = alloc.AddRegion(TEST->reg);
        else
            res = alloc.SubtractRegion(TEST->reg, TEST->incomplete);

        EXPECT_EQ(TEST->res ? MX_OK : MX_ERR_INVALID_ARGS, res);
        EXPECT_EQ(TEST->cnt, alloc.AvailableRegionCount());
    }

    END_TEST;
}

} //namespace

BEGIN_TEST_CASE(ralloc_tests)
RUN_NAMED_TEST("Region Pools",   ralloc_region_pools_test)
RUN_NAMED_TEST("Alloc by size",  ralloc_by_size_test)
RUN_NAMED_TEST("Alloc specific", ralloc_specific_test)
RUN_NAMED_TEST("Add/Overlap",    ralloc_add_overlap_test)
RUN_NAMED_TEST("Subtract",       ralloc_subtract_test)
END_TEST_CASE(ralloc_tests)
