// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

#include <utility>

#include <fbl/algorithm.h>
#include <region-alloc/region-alloc.h>
#include <zxtest/zxtest.h>

#include "common.h"

namespace {

// A simple bool-like enum used to determine if generalized test helper should
// use the heap for bookkeeping, or if it should use a RegionPool slab
// allocator.
enum class TestFlavor {
  UsePool,
  UseHeap,
};

TEST(RegionAllocCppApiTestCase, RegionPools) {
  // Create a default constructed allocator on the stack.
  RegionAllocator alloc;

  // Make a region pool to manage bookkeeping allocations.
  auto pool = RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE);
  ASSERT_NOT_NULL(pool.get());

  {
    // Add a single region to our allocator and then get a region out of the
    // middle of it.  Since we have not yet assigned a RegionPool, all of the
    // bookkeeping for this will be allocated directly from the heap.
    RegionAllocator::Region::UPtr tmp;
    EXPECT_OK(alloc.AddRegion({0u, 1024u}));

    tmp = alloc.GetRegion({128, 256});
    EXPECT_NOT_NULL(tmp);

    // Now attempt to assign a region pool to allocate from.  Since we have both
    // active regions and active allocations, this will fail with BAD_STATE.
    EXPECT_EQ(ZX_ERR_BAD_STATE, alloc.SetRegionPool(pool));

    // Give our allocation back and try again.  This should still fail.  We have
    // no active allocations, but we still have region bookkeeping allocated
    // from the heap, so we cannot change allocators yet.
    tmp.reset();
    EXPECT_EQ(ZX_ERR_BAD_STATE, alloc.SetRegionPool(pool));

    // Finally, release the available region bookkeeping.  This will set us up
    // for success during the rest of the test.
    alloc.Reset();
  }

  // Assign our pool to our allocator, but hold onto the pool for now.
  ASSERT_OK(alloc.SetRegionPool(pool));
  EXPECT_NOT_NULL(pool.get());

  // Create another allocator and transfer ownership of our region pool
  // reference to it.  Then let the allocator go out of scope.
  {
    RegionAllocator alloc2(std::move(pool));
    EXPECT_NULL(pool.get());
  }
  EXPECT_NULL(pool.get());

  // Add some regions to our allocator.
  for (size_t i = 0; i < fbl::count_of(GOOD_REGIONS); ++i) {
    EXPECT_OK(alloc.AddRegion(GOOD_REGIONS[i]));
  }

  // Make a new pool and try to assign it to the allocator.  This should fail
  // because the allocator is currently using resources from its currently
  // assigned pool.
  auto pool2 = RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE);
  ASSERT_NOT_NULL(pool2.get());
  EXPECT_EQ(ZX_ERR_BAD_STATE, alloc.SetRegionPool(pool2));

  // Add a bunch of adjacent regions to our pool.  Try to add so many
  // that we would normally run out of bookkeeping space.  We should not
  // actually run out, however, because the regions should get merged as they
  // get added.
  {
    ralloc_region_t tmp = {.base = GOOD_MERGE_REGION_BASE, .size = GOOD_MERGE_REGION_SIZE};
    for (size_t i = 0; i < OOM_RANGE_LIMIT; ++i) {
      ASSERT_OK(alloc.AddRegion(tmp));
      tmp.base += tmp.size;
    }
  }

  // Attempt (and fail) to add some bad regions (regions which overlap,
  // regions which wrap the address space)
  for (size_t i = 0; i < fbl::count_of(BAD_REGIONS); ++i) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, alloc.AddRegion(BAD_REGIONS[i]));
  }

  // Force the region bookkeeping pool to run out of memory by adding more and
  // more regions until we eventually run out of room.  Make sure that the
  // regions are not adjacent, or the internal bookkeeping will just merge
  // them.
  {
    size_t i;
    ralloc_region_t tmp = {.base = BAD_MERGE_REGION_BASE, .size = BAD_MERGE_REGION_SIZE};
    for (i = 0; i < OOM_RANGE_LIMIT; ++i) {
      zx_status_t res;

      res = alloc.AddRegion(tmp);
      if (res != ZX_OK) {
        EXPECT_EQ(ZX_ERR_NO_MEMORY, res);
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
  EXPECT_OK(alloc.SetRegionPool(std::move(pool2)));
  EXPECT_NULL(pool2.get());
}

void AllocBySizeHelper(TestFlavor flavor) {
  // Make an allocator.  If we are not using the heap for bookkeeping, then make
  // a pool and attach it to the allocator.
  RegionAllocator alloc((flavor == TestFlavor::UsePool)
                            ? RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE)
                            : nullptr);

  // Add our test regions.
  for (size_t i = 0; i < fbl::count_of(ALLOC_BY_SIZE_REGIONS); ++i) {
    ASSERT_OK(alloc.AddRegion(ALLOC_BY_SIZE_REGIONS[i]));
  }

  // Run the alloc by size tests.  Hold onto the regions it allocates so they
  // don't automatically get returned to the pool.
  RegionAllocator::Region::UPtr regions[fbl::count_of(ALLOC_BY_SIZE_TESTS)];

  for (size_t i = 0; i < fbl::count_of(ALLOC_BY_SIZE_TESTS); ++i) {
    const alloc_by_size_alloc_test_t* TEST = ALLOC_BY_SIZE_TESTS + i;
    zx_status_t res = alloc.GetRegion(TEST->size, TEST->align, regions[i]);

    // Make sure we get the test result we were expecting.
    EXPECT_EQ(TEST->res, res);

    // If the allocation claimed to succeed, we should have gotten
    // back a non-null region.  Otherwise, we should have gotten a
    // null region back.
    if (res == ZX_OK) {
      ASSERT_NOT_NULL(regions[i].get());
    } else {
      EXPECT_NULL(regions[i].get());
    }

    // If the allocation succeeded, and we expected it to succeed,
    // the allocation should have come from the test region we
    // expect and be aligned in the way we asked.
    if ((res == ZX_OK) && (TEST->res == ZX_OK)) {
      ASSERT_LT(TEST->region, fbl::count_of(ALLOC_BY_SIZE_TESTS));
      EXPECT_TRUE(region_contains_region(ALLOC_BY_SIZE_REGIONS + TEST->region, regions[i].get()));
      EXPECT_EQ(0u, regions[i]->base & (TEST->align - 1));
    }
  }

  // No need for any explicit cleanup.  Our region references will go out of
  // scope first and be returned to the allocator.  Then the allocator will
  // clean up, and release its bookkeeping pool reference in the process.
}

TEST(RegionAllocCppApiTestCase, AllocBySizeFromPool) {
  ASSERT_NO_FAILURES(AllocBySizeHelper(TestFlavor::UsePool));
}

TEST(RegionAllocCppApiTestCase, AllocBySizeFromHeap) {
  ASSERT_NO_FAILURES(AllocBySizeHelper(TestFlavor::UseHeap));
}

void AllocSpecificHelper(TestFlavor flavor) {
  // Make an allocator.  If we are not using the heap for bookkeeping, then make
  // a pool and attach it to the allocator.
  RegionAllocator alloc((flavor == TestFlavor::UsePool)
                            ? RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE)
                            : nullptr);

  // Add our test regions.
  for (size_t i = 0; i < fbl::count_of(ALLOC_SPECIFIC_REGIONS); ++i) {
    ASSERT_OK(alloc.AddRegion(ALLOC_SPECIFIC_REGIONS[i]));
  }

  // Run the alloc specific tests.  Hold onto the regions it allocates so they
  // don't automatically get returned to the pool.
  RegionAllocator::Region::UPtr regions[fbl::count_of(ALLOC_SPECIFIC_TESTS)];

  for (size_t i = 0; i < fbl::count_of(ALLOC_SPECIFIC_TESTS); ++i) {
    const alloc_specific_alloc_test_t* TEST = ALLOC_SPECIFIC_TESTS + i;
    zx_status_t res = alloc.GetRegion(TEST->req, regions[i]);

    // Make sure we get the test result we were expecting.
    EXPECT_EQ(TEST->res, res);

    // If the allocation claimed to succeed, we should have gotten back a
    // non-null region which exactly matches our requested region.
    if (res == ZX_OK) {
      ASSERT_NOT_NULL(regions[i].get());
      EXPECT_EQ(TEST->req.base, regions[i]->base);
      EXPECT_EQ(TEST->req.size, regions[i]->size);
    } else {
      EXPECT_NULL(regions[i].get());
    }
  }

  // No need for any explicit cleanup.  Our region references will go out of
  // scope first and be returned to the allocator.  Then the allocator will
  // clean up, and release its bookkeeping pool reference in the process.
}

TEST(RegionAllocCppApiTestCase, AllocSpecificFromPool) {
  ASSERT_NO_FAILURES(AllocSpecificHelper(TestFlavor::UsePool));
}

TEST(RegionAllocCppApiTestCase, AllocSpecificFromHeap) {
  ASSERT_NO_FAILURES(AllocSpecificHelper(TestFlavor::UseHeap));
}

void AddOverlapHelper(TestFlavor flavor) {
  // Make an allocator.  If we are not using the heap for bookkeeping, then make
  // a pool and attach it to the allocator.
  RegionAllocator alloc((flavor == TestFlavor::UsePool)
                            ? RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE)
                            : nullptr);

  // Add each of the regions specified by the test and check the expected results.
  for (size_t i = 0; i < fbl::count_of(ADD_OVERLAP_TESTS); ++i) {
    const alloc_add_overlap_test_t* TEST = ADD_OVERLAP_TESTS + i;

    zx_status_t res = alloc.AddRegion(TEST->reg, TEST->ovl);

    EXPECT_EQ(TEST->res, res);
    EXPECT_EQ(TEST->cnt, alloc.AvailableRegionCount());
  }
}

TEST(RegionAllocCppApiTestCase, AddOverlapFromPool) {
  ASSERT_NO_FAILURES(AddOverlapHelper(TestFlavor::UsePool));
}

TEST(RegionAllocCppApiTestCase, AddOverlapFromHeap) {
  ASSERT_NO_FAILURES(AddOverlapHelper(TestFlavor::UseHeap));
}

void SubtractHelper(TestFlavor flavor) {
  // Make an allocator.  If we are not using the heap for bookkeeping, then make
  // a pool and attach it to the allocator.
  RegionAllocator alloc((flavor == TestFlavor::UsePool)
                            ? RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE)
                            : nullptr);

  // Run the test sequence, adding and subtracting regions and verifying the results.
  for (size_t i = 0; i < fbl::count_of(SUBTRACT_TESTS); ++i) {
    const alloc_subtract_test_t* TEST = SUBTRACT_TESTS + i;

    zx_status_t res;
    if (TEST->add)
      res = alloc.AddRegion(TEST->reg);
    else
      res = alloc.SubtractRegion(TEST->reg, TEST->incomplete);

    EXPECT_EQ(TEST->res ? ZX_OK : ZX_ERR_INVALID_ARGS, res);
    EXPECT_EQ(TEST->cnt, alloc.AvailableRegionCount());
  }
}

TEST(RegionAllocCppApiTestCase, SubtractFromPool) {
  ASSERT_NO_FAILURES(SubtractHelper(TestFlavor::UsePool));
}

TEST(RegionAllocCppApiTestCase, SubtractFromHeap) {
  ASSERT_NO_FAILURES(SubtractHelper(TestFlavor::UseHeap));
}

void AllocatedWalkHelper(TestFlavor flavor) {
  // Make an allocator.  If we are not using the heap for bookkeeping, then make
  // a pool and attach it to the allocator.
  RegionAllocator alloc((flavor == TestFlavor::UsePool)
                            ? RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE)
                            : nullptr);

  const ralloc_region_t test_regions[] = {
      {.base = 0x00000000, .size = 1 << 20}, {.base = 0x10000000, .size = 1 << 20},
      {.base = 0x20000000, .size = 1 << 20}, {.base = 0x30000000, .size = 1 << 20},
      {.base = 0x40000000, .size = 1 << 20}, {.base = 0x50000000, .size = 1 << 20},
      {.base = 0x60000000, .size = 1 << 20}, {.base = 0x70000000, .size = 1 << 20},
      {.base = 0x80000000, .size = 1 << 20}, {.base = 0x90000000, .size = 1 << 20},
  };
  constexpr size_t r_cnt = fbl::count_of(test_regions);

  EXPECT_OK(alloc.AddRegion({.base = 0, .size = UINT64_MAX}));

  // Pull each region defined above out of the allocator and stash their UPtrs
  // for the time being.  Then the lambda can walk the allocated regions and
  // verify that they are in-order and match the expected values.
  RegionAllocator::Region::UPtr r[r_cnt];
  for (unsigned i = 0; i < r_cnt; i++) {
    EXPECT_OK(alloc.GetRegion(test_regions[i], r[i]));
  }

  uint8_t pos = 0;
  uint64_t end = 0;
  auto f = [&](const ralloc_region_t* r) -> bool {
    // Make sure the region matches what we expect.  If not, tell the
    // callback to exit the walk operation early.
    check_region_match(r, &test_regions[pos]);
    if (CURRENT_TEST_HAS_FATAL_FAILURES()) {
      return false;
    }

    pos++;

    // attempt to exit early if end is set to a value > 0
    return (end) ? (pos != end) : true;
  };

  ASSERT_NO_FATAL_FAILURES(alloc.WalkAllocatedRegions(f));
  ASSERT_EQ(r_cnt, pos);

  // Test that exiting early works, no matter where we are in the region list.
  // Every time the function is called we increment the counter and then at
  // the end ensure we've only been called as many times as expected, within
  // the bounds of [1, r_cnt].
  for (size_t cnt = 0; cnt < 1024; cnt++) {
    pos = 0;
    end = (rand() % r_cnt) + 1;
    alloc.WalkAllocatedRegions(f);
    ASSERT_EQ(pos, end);
  }
}

TEST(RegionAllocCppApiTestCase, AllocatedWalkFromPool) {
  ASSERT_NO_FAILURES(AllocatedWalkHelper(TestFlavor::UsePool));
}

TEST(RegionAllocCppApiTestCase, AllocatedWalkFromHeap) {
  ASSERT_NO_FAILURES(AllocatedWalkHelper(TestFlavor::UseHeap));
}

void TestRegionHelper(TestFlavor flavor) {
  // Make an allocator, and if we are not using the heap for bookkeeping, make a
  // pool and attach it to an allocator.
  RegionAllocator alloc((flavor == TestFlavor::UsePool)
                            ? RegionAllocator::RegionPool::Create(REGION_POOL_MAX_SIZE)
                            : nullptr);

  // Put the allocator into the state we want for testing.  We want a situation
  // where there are at least 3 regions in the available set, and 3 regions in
  // the allocated set.
  const ralloc_region_t test_regions[] = {
      {.base = 0x1000, .size = 0x2000},
      {.base = 0x4000, .size = 0x2000},
      {.base = 0x8000, .size = 0x2000},
  };

  struct AllocatedRegion {
    // Allow implicit construction to make our notation in the table of regions
    // below just a bit shorter.
    AllocatedRegion(const ralloc_region_t& r) : region(r) {}
    const ralloc_region_t region;
    RegionAllocator::Region::UPtr ptr;
  } allocated_regions[] = {
      {{.base = 0x1000, .size = 0x1000}},
      {{.base = 0x4800, .size = 0x1000}},
      {{.base = 0x9000, .size = 0x1000}},
  };

  // Add the initial available regions to the set
  for (const auto& r : test_regions) {
    ASSERT_OK(alloc.AddRegion(r));
  }

  // Take out the initial "allocated" set, making sure to hold onto the
  // allocation references.
  for (auto& ar : allocated_regions) {
    ar.ptr = alloc.GetRegion(ar.region);
  }

  // OK, at this point we should have an allocator with the following available
  // and allocated regions.
  //
  // :: Allocated ::
  // [ 0x1000, 0x1FFF ],
  // [ 0x4800, 0x57FF ],
  // [ 0x9000, 0x9FFF ],
  //
  // :: Avail ::
  // [ 0x2000, 0x2FFF ],
  // [ 0x4000, 0x47FF ],
  // [ 0x5800, 0x5FFF ],
  // [ 0x8000, 0x8FFF ],
  //
  // Now just create a set of test vectors which attempts to hit all of the edge
  // cases here.  Each vector is flagged with an expectations to
  // intersect/be-contained-by regions in the allocated/available region sets.
  const struct {
    ralloc_region_t region;
    bool ai, ac;  // allocated intersects/contained by
    bool vi, vc;  // available intersects/contained_by
  } test_vectors[] = {
      // clang-format off
    { {.base = 0x0000, .size = 0xF000 }, .ai =  true, .ac = false, .vi =  true, .vc = false },
    { {.base = 0x0000, .size =  0x100 }, .ai = false, .ac = false, .vi = false, .vc = false },

    { {.base = 0x0FF0, .size =   0x10 }, .ai = false, .ac = false, .vi = false, .vc = false },
    { {.base = 0x0FF1, .size =   0x10 }, .ai =  true, .ac = false, .vi = false, .vc = false },
    { {.base = 0x1000, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x1010, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x1FF0, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x1FF8, .size =   0x10 }, .ai =  true, .ac = false, .vi =  true, .vc = false },

    { {.base = 0x2000, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x2010, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x2FF0, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x2FF8, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc = false },
    { {.base = 0x3000, .size =   0x10 }, .ai = false, .ac = false, .vi = false, .vc = false },

    { {.base = 0x3FF0, .size =   0x10 }, .ai = false, .ac = false, .vi = false, .vc = false },
    { {.base = 0x3FF1, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc = false },
    { {.base = 0x4000, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x4010, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x47F0, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x47F8, .size =   0x10 }, .ai =  true, .ac = false, .vi =  true, .vc = false },

    { {.base = 0x4800, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x4900, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x57F0, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x57F8, .size =   0x10 }, .ai =  true, .ac = false, .vi =  true, .vc = false },

    { {.base = 0x5800, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x5900, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x5FF0, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x5FF8, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc = false },
    { {.base = 0x6000, .size =   0x10 }, .ai = false, .ac = false, .vi = false, .vc = false },

    { {.base = 0x7FF0, .size =   0x10 }, .ai = false, .ac = false, .vi = false, .vc = false },
    { {.base = 0x7FF1, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc = false },
    { {.base = 0x8000, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x8010, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x8FF0, .size =   0x10 }, .ai = false, .ac = false, .vi =  true, .vc =  true },
    { {.base = 0x8FF8, .size =   0x10 }, .ai =  true, .ac = false, .vi =  true, .vc = false },

    { {.base = 0x9000, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x9010, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x9FF0, .size =   0x10 }, .ai =  true, .ac =  true, .vi = false, .vc = false },
    { {.base = 0x9FF8, .size =   0x10 }, .ai =  true, .ac = false, .vi = false, .vc = false },
    { {.base = 0xA000, .size =   0x10 }, .ai = false, .ac = false, .vi = false, .vc = false },
      // clang-format on
  };

  for (const auto& tv : test_vectors) {
    using TRS = RegionAllocator::TestRegionSet;
    uint64_t s = tv.region.base;
    uint64_t e = tv.region.base + tv.region.size - 1;
    EXPECT_EQ(tv.ai, alloc.TestRegionIntersects(tv.region, TRS::Allocated),
              "Region [0x%lx, 0x%lx] should %sintersect by the allocated set, but is%s.", s, e,
              tv.ai ? "" : "not ", tv.ai ? "not " : "");
    EXPECT_EQ(tv.ac, alloc.TestRegionContainedBy(tv.region, TRS::Allocated),
              "Region [0x%lx, 0x%lx] should %sbe contained by the allocated set, but is%s.", s, e,
              tv.ac ? "" : "not ", tv.ac ? "not " : "");
    EXPECT_EQ(tv.vi, alloc.TestRegionIntersects(tv.region, TRS::Available),
              "Region [0x%lx, 0x%lx] should %sintersect by the available set, but is%s.", s, e,
              tv.vi ? "" : "not ", tv.vi ? "not " : "");
    EXPECT_EQ(tv.vc, alloc.TestRegionContainedBy(tv.region, TRS::Available),
              "Region [0x%lx, 0x%lx] should %sbe contained by the available set, but is%s.", s, e,
              tv.vc ? "" : "not ", tv.vc ? "not " : "");
  }

  // We should be done now.  When allocated_regions goes out of scope, it will
  // release our allocations.  Then the allocator will go out of scope releasing
  // all of the bookkeeping and the RegionPool (if we used one) in the process.
}

TEST(RegionAllocCppApiTestCase, TestRegionFromPool) {
  ASSERT_NO_FAILURES(TestRegionHelper(TestFlavor::UsePool));
}

TEST(RegionAllocCppApiTestCase, TestRegionFromHeap) {
  ASSERT_NO_FAILURES(TestRegionHelper(TestFlavor::UseHeap));
}

}  // namespace
