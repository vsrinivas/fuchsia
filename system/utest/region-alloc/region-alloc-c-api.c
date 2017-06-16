// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <region-alloc/region-alloc.h>
#include <stdio.h>
#include <unittest/unittest.h>

#include "common.h"

static bool ralloc_pools_c_api_test(void) {
    BEGIN_TEST;

    // Make a pool for the bookkeeping.  Do not allow it to be very large.
    // Require that this succeeds, we will not be able to run the tests without
    // it.
    ralloc_pool_t* pool;
    ASSERT_EQ(MX_OK, ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool), "");
    ASSERT_NONNULL(pool, "");

    // Create an allocator.
    ralloc_allocator_t* alloc;
    ASSERT_EQ(MX_OK, ralloc_create_allocator(&alloc), "");
    ASSERT_NONNULL(alloc, "");

    {
        // Make sure that it refuses to perform any operations because it has no
        // RegionPool assigned to it yet.
        const ralloc_region_t tmp = { .base = 0u, .size = 1u };
        const ralloc_region_t* out;

        EXPECT_EQ(MX_ERR_BAD_STATE, ralloc_add_region(alloc, &tmp, false), "");
        EXPECT_EQ(MX_ERR_BAD_STATE, ralloc_get_sized_region_ex(alloc, 1u, 1u, &out), "");
        EXPECT_EQ(MX_ERR_BAD_STATE, ralloc_get_specific_region_ex(alloc, &tmp, &out), "");
        EXPECT_NULL(ralloc_get_sized_region(alloc, 1u, 1u), "");
        EXPECT_NULL(ralloc_get_specific_region(alloc, &tmp), "");
    }

    // Assign our pool to our allocator, but hold onto the pool for now.
    EXPECT_EQ(MX_OK, ralloc_set_region_pool(alloc, pool), "");

    // Release our pool reference.  The allocator should be holding onto its own
    // reference at this point.
    ralloc_release_pool(pool);
    pool = NULL;

    // Add some regions to our allocator.
    for (size_t i = 0; i < countof(GOOD_REGIONS); ++i)
        EXPECT_EQ(MX_OK, ralloc_add_region(alloc, &GOOD_REGIONS[i], false), "");

    // Make a new pool and try to assign it to the allocator.  This should fail
    // because the allocator is currently using resources from its currently
    // assigned pool.
    ASSERT_EQ(MX_OK, ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool), "");
    ASSERT_NONNULL(pool, "");
    EXPECT_EQ(MX_ERR_BAD_STATE, ralloc_set_region_pool(alloc, pool), "");

    // Add a bunch of adjacent regions to our pool.  Try to add so many
    // that we would normally run out of bookkeeping space.  We should not
    // actually run out, however, because the regions should get merged as they
    // get added.
    {
        ralloc_region_t tmp = { .base = GOOD_MERGE_REGION_BASE,
                                     .size = GOOD_MERGE_REGION_SIZE };
        for (size_t i = 0; i < OOM_RANGE_LIMIT; ++i) {
            ASSERT_EQ(MX_OK, ralloc_add_region(alloc, &tmp, false), "");
            tmp.base += tmp.size;
        }
    }

    // Attempt (and fail) to add some bad regions (regions which overlap,
    // regions which wrap the address space)
    for (size_t i = 0; i < countof(BAD_REGIONS); ++i)
        EXPECT_EQ(MX_ERR_INVALID_ARGS, ralloc_add_region(alloc, &BAD_REGIONS[i], false), "");

    // Force the region bookkeeping pool to run out of memory by adding more and
    // more regions until we eventuall run out of room.  Make sure that the
    // regions are not adjacent, or the internal bookkeeping will just merge
    // them.
    {
        size_t i;
        ralloc_region_t tmp = { .base = BAD_MERGE_REGION_BASE,
                                .size = BAD_MERGE_REGION_SIZE };
        for (i = 0; i < OOM_RANGE_LIMIT; ++i) {
            mx_status_t res;

            res = ralloc_add_region(alloc, &tmp, false);
            if (res != MX_OK) {
                EXPECT_EQ(MX_ERR_NO_MEMORY, res, "");
                break;
            }

            tmp.base += tmp.size + 1;
        }

        EXPECT_LT(i, OOM_RANGE_LIMIT, "");
    }

    // Reset allocator.  All of the existing available regions we had previously
    // added will be returned to the pool.
    ralloc_reset_allocator(alloc);

    // Now assign the second pool to the allocator.  Now that the allocator is
    // no longer using any resources, this should succeed.
    EXPECT_EQ(MX_OK, ralloc_set_region_pool(alloc, pool), "");

    // Release our pool reference.
    ralloc_release_pool(pool);

    // Destroy our allocator.
    ralloc_destroy_allocator(alloc);

    END_TEST;
}

static bool ralloc_by_size_c_api_test(void) {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.  Then add the test regions to it.
    ralloc_allocator_t* alloc = NULL;
    {
        ralloc_pool_t* pool;
        ASSERT_EQ(MX_OK, ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool), "");
        ASSERT_NONNULL(pool, "");

        // Create an allocator and add our region pool to it.
        ASSERT_EQ(MX_OK, ralloc_create_allocator(&alloc), "");
        ASSERT_NONNULL(alloc, "");
        ASSERT_EQ(MX_OK, ralloc_set_region_pool(alloc, pool), "");

        // Release our pool reference.  The allocator should be holding onto its own
        // reference at this point.
        ralloc_release_pool(pool);
    }

    for (size_t i = 0; i < countof(ALLOC_BY_SIZE_REGIONS); ++i)
        EXPECT_EQ(MX_OK, ralloc_add_region(alloc, &ALLOC_BY_SIZE_REGIONS[i], false), "");

    // Run the alloc by size tests.  Hold onto the regions it allocates so they
    // can be cleaned up properly when the test finishes.
    const ralloc_region_t* regions[countof(ALLOC_BY_SIZE_TESTS)];
    memset(regions, 0, sizeof(regions));

    for (size_t i = 0; i < countof(ALLOC_BY_SIZE_TESTS); ++i) {
        const alloc_by_size_alloc_test_t* TEST = ALLOC_BY_SIZE_TESTS + i;
        mx_status_t res = ralloc_get_sized_region_ex(alloc,
                                                     TEST->size,
                                                     TEST->align,
                                                     regions + i);

        // Make sure we get the test result we were expecting.
        EXPECT_EQ(TEST->res, res, "");

        // If the allocation claimed to succeed, we should have gotten
        // back a non-null region.  Otherwise, we should have gotten a
        // null region back.
        if (res == MX_OK) {
            ASSERT_NONNULL(regions[i], "");
        } else {
            EXPECT_NULL(regions[i], "");
        }

        // If the allocation succeeded, and we expected it to succeed,
        // the allocation should have come from the test region we
        // expect and be aligned in the way we asked.
        if ((res == MX_OK) && (TEST->res == MX_OK)) {
            ASSERT_LT(TEST->region, countof(ALLOC_BY_SIZE_TESTS), "");
            EXPECT_TRUE(region_contains_region(ALLOC_BY_SIZE_REGIONS + TEST->region,
                                               regions[i]), "");
            EXPECT_EQ(0u, regions[i]->base & (TEST->align - 1), "");
        }
    }

    // Put the regions we have allocated back in the allocator.
    for (size_t i = 0; i < countof(regions); ++i)
        if (regions[i])
            ralloc_put_region(regions[i]);

    // Destroy our allocator.
    ralloc_destroy_allocator(alloc);

    END_TEST;
}


static bool ralloc_specific_c_api_test(void) {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.  Then add the test regions to it.
    ralloc_allocator_t* alloc = NULL;
    {
        ralloc_pool_t* pool;
        ASSERT_EQ(MX_OK, ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool), "");
        ASSERT_NONNULL(pool, "");

        // Create an allocator and add our region pool to it.
        ASSERT_EQ(MX_OK, ralloc_create_allocator(&alloc), "");
        ASSERT_NONNULL(alloc, "");
        ASSERT_EQ(MX_OK, ralloc_set_region_pool(alloc, pool), "");

        // Release our pool reference.  The allocator should be holding onto its own
        // reference at this point.
        ralloc_release_pool(pool);
    }

    for (size_t i = 0; i < countof(ALLOC_SPECIFIC_REGIONS); ++i)
        EXPECT_EQ(MX_OK, ralloc_add_region(alloc, &ALLOC_SPECIFIC_REGIONS[i], false), "");

    // Run the alloc by size tests.  Hold onto the regions it allocates so they
    // can be cleaned up properly when the test finishes.
    const ralloc_region_t* regions[countof(ALLOC_SPECIFIC_TESTS)];
    memset(regions, 0, sizeof(regions));

    for (size_t i = 0; i < countof(ALLOC_SPECIFIC_TESTS); ++i) {
        const alloc_specific_alloc_test_t* TEST = ALLOC_SPECIFIC_TESTS + i;
        mx_status_t res = ralloc_get_specific_region_ex(alloc, &TEST->req, regions + i);

        // Make sure we get the test result we were expecting.
        EXPECT_EQ(TEST->res, res, "");

        // If the allocation claimed to succeed, we should have gotten back a
        // non-null region which exactly matches our requested region.
        if (res == MX_OK) {
            ASSERT_NONNULL(regions[i], "");
            EXPECT_EQ(TEST->req.base, regions[i]->base, "");
            EXPECT_EQ(TEST->req.size, regions[i]->size, "");
        } else {
            EXPECT_NULL(regions[i], "");
        }
    }

    // Put the regions we have allocated back in the allocator.
    for (size_t i = 0; i < countof(regions); ++i)
        if (regions[i])
            ralloc_put_region(regions[i]);

    // Destroy our allocator.
    ralloc_destroy_allocator(alloc);

    END_TEST;
}

static bool ralloc_add_overlap_c_api_test(void) {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.
    ralloc_allocator_t* alloc = NULL;
    {
        ralloc_pool_t* pool;
        ASSERT_EQ(MX_OK, ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool), "");
        ASSERT_NONNULL(pool, "");

        // Create an allocator and add our region pool to it.
        ASSERT_EQ(MX_OK, ralloc_create_allocator(&alloc), "");
        ASSERT_NONNULL(alloc, "");
        ASSERT_EQ(MX_OK, ralloc_set_region_pool(alloc, pool), "");

        // Release our pool reference.  The allocator should be holding onto its own
        // reference at this point.
        ralloc_release_pool(pool);
    }

    // Add each of the regions specified by the test and check the expected results.
    for (size_t i = 0; i < countof(ADD_OVERLAP_TESTS); ++i) {
        const alloc_add_overlap_test_t* TEST = ADD_OVERLAP_TESTS + i;

        mx_status_t res = ralloc_add_region(alloc, &TEST->reg, TEST->ovl);

        EXPECT_EQ(TEST->res, res, "");
        EXPECT_EQ(TEST->cnt, ralloc_get_available_region_count(alloc), "");
    }

    // Destroy our allocator.
    ralloc_destroy_allocator(alloc);

    END_TEST;
}

static bool ralloc_subtract_c_api_test(void) {
    BEGIN_TEST;

    // Make a pool and attach it to an allocator.
    ralloc_allocator_t* alloc = NULL;
    {
        ralloc_pool_t* pool;
        ASSERT_EQ(MX_OK, ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool), "");
        ASSERT_NONNULL(pool, "");

        // Create an allocator and add our region pool to it.
        ASSERT_EQ(MX_OK, ralloc_create_allocator(&alloc), "");
        ASSERT_NONNULL(alloc, "");
        ASSERT_EQ(MX_OK, ralloc_set_region_pool(alloc, pool), "");

        // Release our pool reference.  The allocator should be holding onto its own
        // reference at this point.
        ralloc_release_pool(pool);
    }

    // Run the test sequence, adding and subtracting regions and verifying the results.
    for (size_t i = 0; i < countof(SUBTRACT_TESTS); ++i) {
        const alloc_subtract_test_t* TEST = SUBTRACT_TESTS + i;

        mx_status_t res;
        if (TEST->add)
            res = ralloc_add_region(alloc, &TEST->reg, false);
        else
            res = ralloc_sub_region(alloc, &TEST->reg, TEST->incomplete);

        EXPECT_EQ(TEST->res ? MX_OK : MX_ERR_INVALID_ARGS, res, "");
        EXPECT_EQ(TEST->cnt, ralloc_get_available_region_count(alloc), "");
    }

    // Destroy our allocator.
    ralloc_destroy_allocator(alloc);

    END_TEST;
}

BEGIN_TEST_CASE(ralloc_c_api_tests)
RUN_NAMED_TEST("Region Pools (C-API)",   ralloc_pools_c_api_test)
RUN_NAMED_TEST("Alloc by size (C-API)",  ralloc_by_size_c_api_test)
RUN_NAMED_TEST("Alloc specific (C-API)", ralloc_specific_c_api_test)
RUN_NAMED_TEST("Subtract (C-API)",       ralloc_subtract_c_api_test)
END_TEST_CASE(ralloc_c_api_tests)
