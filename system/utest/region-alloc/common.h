// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <region-alloc/region-alloc.h>
#include <stddef.h>

// Constants and common tables used by both the C and C++ API tests.
#ifdef __cplusplus
static constexpr size_t REGION_POOL_MAX_SIZE = (RegionAllocator::RegionPool::SLAB_SIZE << 1);
#else
#define REGION_POOL_MAX_SIZE  (REGION_POOL_SLAB_SIZE << 1)
#endif

#define OOM_RANGE_LIMIT (1000u)

#define GOOD_MERGE_REGION_BASE ((uint64_t)0x3000000000000000)
#define GOOD_MERGE_REGION_SIZE (16u << 10)

#define BAD_MERGE_REGION_BASE ((uint64_t)0x4000000000000000)
#define BAD_MERGE_REGION_SIZE (16u << 10)

static const ralloc_region_t GOOD_REGIONS[] = {
    { .base = 0x10000000,                   .size = 256 << 10 },
    { .base = 0x20000000 - 1 * (256 << 10), .size = 256 << 10 },
    { .base = 0x20000000 + 3 * (256 << 10), .size = 256 << 10 },
    { .base = 0x20000000,                   .size = 256 << 10 },  // Merges with before (ndx 1)
    { .base = 0x20000000 + 2 * (256 << 10), .size = 256 << 10 },  // Merges with after (ndx 2)
    { .base = 0x20000000 + 1 * (256 << 10), .size = 256 << 10 },  // Merges with before/after
    { .base = 0x1000000000000000,           .size = 256 << 10 },
    { .base = 0x2000000000000000,           .size = 256 << 10 },
};

static const ralloc_region_t BAD_REGIONS[] = {
    { .base = 0x10000000 - (256 << 10) + 1, .size = 256 << 10 },
    { .base = 0x10000000 - 1,               .size = 256 << 10 },
    { .base = 0x10000000 + (256 << 10) - 1, .size = 256 << 10 },
    { .base = 0x10000000 - 1,               .size = 512 << 10 },
    { .base = 0x10000000 + 1,               .size = 128 << 10 },

    { .base = 0x1000000000000000 - (256 << 10) + 1, .size = 256 << 10 },
    { .base = 0x1000000000000000 - 1,               .size = 256 << 10 },
    { .base = 0x1000000000000000 + (256 << 10) - 1, .size = 256 << 10 },
    { .base = 0x1000000000000000 - 1,               .size = 512 << 10 },
    { .base = 0x1000000000000000 + 1,               .size = 128 << 10 },

    { .base = 0x2000000000000000 - (256 << 10) + 1, .size = 256 << 10 },
    { .base = 0x2000000000000000 - 1,               .size = 256 << 10 },
    { .base = 0x2000000000000000 + (256 << 10) - 1, .size = 256 << 10 },
    { .base = 0x2000000000000000 - 1,               .size = 512 << 10 },
    { .base = 0x2000000000000000 + 1,               .size = 128 << 10 },

    { .base = 0xFFFFFFFFFFFFFFFF, .size = 0x1 },
    { .base = 0xFFFFFFFF00000000, .size = 0x100000000 },
};

static inline bool region_contains_region(
    const ralloc_region_t* contained_by,
    const ralloc_region_t* contained) {
    uint64_t contained_end    = contained->base    + contained->size - 1;
    uint64_t contained_by_end = contained_by->base + contained_by->size - 1;

    return ((contained->base >= contained_by->base) &&
            (contained_end   >= contained_by->base) &&
            (contained->base <= contained_by_end) &&
            (contained_end   <= contained_by_end));
}

#define ALLOC_BY_SIZE_SMALL_REGION_BASE (0x0)       // All alignments
#define ALLOC_BY_SIZE_SMALL_REGION_SIZE (4 << 10)   // 4KB slice

#define ALLOC_BY_SIZE_LARGE_REGION_BASE (0x100000)  // 1MB alignment
#define ALLOC_BY_SIZE_LARGE_REGION_SIZE (1 << 20)   // 1MB slice

static const ralloc_region_t ALLOC_BY_SIZE_REGIONS[] = {
    { .base = ALLOC_BY_SIZE_SMALL_REGION_BASE, .size = ALLOC_BY_SIZE_SMALL_REGION_SIZE },
    { .base = ALLOC_BY_SIZE_LARGE_REGION_BASE, .size = ALLOC_BY_SIZE_LARGE_REGION_SIZE },
};

typedef struct {
    uint64_t    size;
    uint64_t    align;
    mx_status_t res;
    size_t      region;
} alloc_by_size_alloc_test_t;

static const alloc_by_size_alloc_test_t ALLOC_BY_SIZE_TESTS[] = {
    // Invalid parameter failures
    { .size = 0x00000000, .align = 0x00000001, .res = MX_ERR_INVALID_ARGS, 0 },  // bad size
    { .size = 0x00000001, .align = 0x00000000, .res = MX_ERR_INVALID_ARGS, 0 },  // bad align
    { .size = 0x00000001, .align = 0x00001001, .res = MX_ERR_INVALID_ARGS, 0 },  // bad align

    // Initially unsatisfiable
    { .size = 0x10000000, .align = 0x00000001, .res = MX_ERR_NOT_FOUND, 0 },  // too large
    { .size = 0x00005000, .align = 0x10000000, .res = MX_ERR_NOT_FOUND, 0 },  // Cannot align

    // Should succeed, all pulled from first chunk
    { .size = (1 <<  0), .align = (1 <<  1), .res = MX_OK, .region = 0 },
    { .size = (1 <<  1), .align = (1 <<  2), .res = MX_OK, .region = 0 },
    { .size = (1 <<  2), .align = (1 <<  3), .res = MX_OK, .region = 0 },
    { .size = (1 <<  3), .align = (1 <<  4), .res = MX_OK, .region = 0 },
    { .size = (1 <<  4), .align = (1 <<  5), .res = MX_OK, .region = 0 },
    { .size = (1 <<  5), .align = (1 <<  6), .res = MX_OK, .region = 0 },
    { .size = (1 <<  6), .align = (1 <<  7), .res = MX_OK, .region = 0 },
    { .size = (1 <<  7), .align = (1 <<  8), .res = MX_OK, .region = 0 },
    { .size = (1 <<  8), .align = (1 <<  9), .res = MX_OK, .region = 0 },
    { .size = (1 <<  9), .align = (1 << 10), .res = MX_OK, .region = 0 },
    { .size = (1 << 10), .align = (1 << 11), .res = MX_OK, .region = 0 },

    // Perform some allocations which are large enough that they can only be
    // satisfied with results from region 1.  Exercise the various range
    // splitting cases.
    { .size = (4 << 10), .align = (4 << 10), .res = MX_OK, .region = 1 }, // front of region 1
    { .size = (4 << 10), .align = (4 << 11), .res = MX_OK, .region = 1 }, // middle of region 1
    { .size = 0xfc000,   .align = (4 << 12), .res = MX_OK, .region = 1 }, // back of region 1

    // Repeat the small allocation pass again.  Because of the alignment
    // restrictions, the first pass should have fragmented the first region.
    // This pass should soak up those fragments.
    { .size = (3),       .align = (1 <<  0), .res = MX_OK, .region = 0 },
    { .size = (1 <<  1), .align = (1 <<  1), .res = MX_OK, .region = 0 },
    { .size = (1 <<  2), .align = (1 <<  2), .res = MX_OK, .region = 0 },
    { .size = (1 <<  3), .align = (1 <<  3), .res = MX_OK, .region = 0 },
    { .size = (1 <<  4), .align = (1 <<  4), .res = MX_OK, .region = 0 },
    { .size = (1 <<  5), .align = (1 <<  5), .res = MX_OK, .region = 0 },
    { .size = (1 <<  6), .align = (1 <<  6), .res = MX_OK, .region = 0 },
    { .size = (1 <<  7), .align = (1 <<  7), .res = MX_OK, .region = 0 },
    { .size = (1 <<  8), .align = (1 <<  8), .res = MX_OK, .region = 0 },
    { .size = (1 <<  9), .align = (1 <<  9), .res = MX_OK, .region = 0 },
    { .size = (1 << 10), .align = (1 << 10), .res = MX_OK, .region = 0 },

    // Region 0 should be exhausted at this point.  Asking for even one more
    // byte should give us an allocation from from region 1.
    { .size = 1, .align = 1, .res = MX_OK, .region = 1 },

    // All that should be left in the pool is a 4k region and a 4k - 1 byte
    // region.  Ask for two 4k regions with arbitrary alignment.  The first
    // request should succeed while the second request should fail.
    { .size = (4 << 10), .align = 1, .res = MX_OK, .region = 1 },
    { .size = (4 << 10), .align = 1, .res = MX_ERR_NOT_FOUND, 0 },

    // Finally, soak up the last of the space with a 0xFFF byte allocation.
    // Afterwards, we should be unable to allocate even a single byte
    { .size = 0xFFF, .align = 1, .res = MX_OK, .region = 1 },
    { .size = 1,     .align = 1, .res = MX_ERR_NOT_FOUND, 0 },
};

#define ALLOC_SPECIFIC_REGION_BASE (0x1000)
#define ALLOC_SPECIFIC_REGION_SIZE (4 << 10)

static const ralloc_region_t ALLOC_SPECIFIC_REGIONS[] = {
    { .base = ALLOC_SPECIFIC_REGION_BASE, .size = ALLOC_SPECIFIC_REGION_SIZE },
};

typedef struct {
    ralloc_region_t req;
    mx_status_t     res;
} alloc_specific_alloc_test_t;

static const alloc_specific_alloc_test_t ALLOC_SPECIFIC_TESTS[] = {
    // Invalid parameter failures
    { .req = { .base = 0x0000000000000000, .size = 0x00 }, .res = MX_ERR_INVALID_ARGS },  // 0 size
    { .req = { .base = 0xffffffffffffffff, .size = 0x01 }, .res = MX_ERR_INVALID_ARGS },  // wraps
    { .req = { .base = 0xfffffffffffffff0, .size = 0x20 }, .res = MX_ERR_INVALID_ARGS },  // wraps

    // Bad requests
    { .req = { .base = 0x0800, .size =   0x1 }, .res = MX_ERR_NOT_FOUND },  // total miss
    { .req = { .base = 0x0fff, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },  // clips the front
    { .req = { .base = 0x1f01, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },  // clips the back
    { .req = { .base = 0x2000, .size =   0x1 }, .res = MX_ERR_NOT_FOUND },  // total miss

    // Good requests
    { .req = { .base = 0x1000, .size = 0x100 }, .res = MX_OK },  // front of range.
    { .req = { .base = 0x1f00, .size = 0x100 }, .res = MX_OK },  // back of range.
    { .req = { .base = 0x1700, .size = 0x200 }, .res = MX_OK },  // middle of range.

    // Requests which would have been good initially, but are bad now.
    { .req = { .base = 0x1000, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x1080, .size =  0x80 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x10ff, .size =   0x1 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x10ff, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },

    { .req = { .base = 0x1f00, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x1e01, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x1e81, .size =  0x80 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x1eff, .size =   0x2 }, .res = MX_ERR_NOT_FOUND },

    { .req = { .base = 0x1800, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x1880, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },
    { .req = { .base = 0x1780, .size = 0x100 }, .res = MX_ERR_NOT_FOUND },

    // Soak up the remaining regions.  There should be 2 left.
    { .req = { .base = 0x1100, .size = 0x600 }, .res = MX_OK },
    { .req = { .base = 0x1900, .size = 0x600 }, .res = MX_OK },
};

typedef struct {
    ralloc_region_t reg;    // Region to add
    bool            ovl;    // Whether to allow overlap or not.
    size_t          cnt;    // Expected available region count afterwards.
    mx_status_t     res;    // Expected result.
} alloc_add_overlap_test_t;

static const alloc_add_overlap_test_t ADD_OVERLAP_TESTS[] = {
    // Add a region, then try to add it again without allowing overlap.  This
    // should fail.  Then add the region again, this time allowing overlap.
    // This should succeed.
    { .reg = { .base = 0x10000, .size = 0x1000 }, .ovl = false, .cnt = 1, .res = MX_OK },
    { .reg = { .base = 0x10000, .size = 0x1000 }, .ovl = false, .cnt = 1, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0x10000, .size = 0x1000 }, .ovl = true,  .cnt = 1, .res = MX_OK },

    // Current: [0x10000, 0x11000)
    // Add a region to the front which fits perfectly with the existing region.
    // This should succeed, even when we do not allow overlapping.
    { .reg = { .base = 0xF800,  .size = 0x800 },  .ovl = false, .cnt = 1, .res = MX_OK },
    { .reg = { .base = 0xF800,  .size = 0x800 },  .ovl = true,  .cnt = 1, .res = MX_OK },

    // Current: [0xF800, 0x11000)
    // Same exercise, but this time add to the back.
    { .reg = { .base = 0x11000, .size = 0x800 },  .ovl = false, .cnt = 1, .res = MX_OK },
    { .reg = { .base = 0x11000, .size = 0x800 },  .ovl = true,  .cnt = 1, .res = MX_OK },

    // Current: [0xF800, 0x11800)
    // Now attempt to add a region which overlaps the front by a single byte.
    // This should fail unless we explicitly permit it.
    { .reg = { .base = 0xF000,  .size = 0x801 },  .ovl = false, .cnt = 1, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0xF000,  .size = 0x801 },  .ovl = true,  .cnt = 1, .res = MX_OK },

    // Current: [0xF000, 0x12000)
    // Same exercise, this time adding to the back.
    { .reg = { .base = 0x117FF, .size = 0x801 },  .ovl = false, .cnt = 1, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0x117FF, .size = 0x801 },  .ovl = true,  .cnt = 1, .res = MX_OK },

    // Current: [0xE000, 0x13000)
    // Add a region which completely contains the existing region.
    { .reg = { .base = 0xE000,  .size = 0x5000 }, .ovl = false, .cnt = 1, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0xE000,  .size = 0x5000 }, .ovl = true,  .cnt = 1, .res = MX_OK },

    // Add some regions which are not connected to the existing region.
    { .reg = { .base = 0x14000, .size = 0x1000 }, .ovl = false, .cnt = 2, .res = MX_OK },
    { .reg = { .base = 0x16000, .size = 0x1000 }, .ovl = false, .cnt = 3, .res = MX_OK },
    { .reg = { .base = 0x18000, .size = 0x1000 }, .ovl = false, .cnt = 4, .res = MX_OK },
    { .reg = { .base = 0x1A000, .size = 0x1000 }, .ovl = false, .cnt = 5, .res = MX_OK },
    { .reg = { .base = 0x1C000, .size = 0x1000 }, .ovl = false, .cnt = 6, .res = MX_OK },

    // Current: [0xE000,  0x13000) [0x14000, 0x15000) [0x16000, 0x17000) [0x18000, 0x19000)
    //          [0x1A000, 0x1B000) [0x1C000, 0x1D000)

    // Add a region which ties two regions together.
    { .reg = { .base = 0x12FFF, .size = 0x1002 }, .ovl = false, .cnt = 6, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0x12FFF, .size = 0x1002 }, .ovl = true,  .cnt = 5, .res = MX_OK },

    // Current: [0xE000,  0x15000) [0x16000, 0x17000) [0x18000, 0x19000) [0x1A000, 0x1B000)
    //          [0x1C000, 0x1D000)

    // Add a region which completely consumes one region, and intersects the
    // front of another.
    { .reg = { .base = 0x15800, .size = 0x3000 }, .ovl = false, .cnt = 5, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0x15800, .size = 0x3000 }, .ovl = true,  .cnt = 4, .res = MX_OK },

    // Current: [0xE000,  0x15000) [0x15800, 0x19000) [0x1A000, 0x1B000) [0x1C000, 0x1D000)

    // Same test as before, but this time from the end.
    { .reg = { .base = 0x18800, .size = 0x3000 }, .ovl = false, .cnt = 4, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0x18800, .size = 0x3000 }, .ovl = true,  .cnt = 3, .res = MX_OK },

    // Current: [0xE000,  0x15000) [0x15800, 0x1B800) [0x1C000, 0x1D000)

    // Add one more region, this one should consume and unify all regions in the
    // set.
    { .reg = { .base = 0xD000, .size = 0x11000 }, .ovl = false, .cnt = 3, .res = MX_ERR_INVALID_ARGS },
    { .reg = { .base = 0xD000, .size = 0x11000 }, .ovl = true,  .cnt = 1, .res = MX_OK },

    // Current: [0xD000,  0x1E000)
};

typedef struct {
    ralloc_region_t reg;        // Region to add or subtract
    bool            add;        // Whether to this is an add operation or not.
    bool            incomplete; // If subtracting, do we allow incomplete subtraction?
    size_t          cnt;        // Expected available region count the operation.
    bool            res;        // Whether we expect succes MX_ERR_INVALID_ARGS.
} alloc_subtract_test_t;

// Temp macro to help make the test table pretty.
#define REG(_b, _s) { .base = (_b), .size = (_s) }

static const alloc_subtract_test_t SUBTRACT_TESTS[] = {
    // Try to subtract a region while the allocator is empty.  This should fail unless we allow
    // incomplete subtraction.
    { .reg = REG(0x1000, 0x1000), .add = false, .incomplete = false, .cnt = 0, .res = false },
    { .reg = REG(0x1000, 0x1000), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // allow_incomplete == false
    // Tests where incomplete subtraction is not allowed.

    // Add a region, then subtract it out.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1000, 0x1000), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then trim the front of it.  Finally, cleanup by removing
    // the specific regions which should be left.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1800,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then trim the back of it.  Then cleanup.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1800,  0x800), .add = false, .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then punch a hole in the middle of it. then cleanup.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1600,  0x400), .add = false, .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x1000,  0x600), .add = false, .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1A00,  0x600), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then fail to remove parts of it with a number of attempts
    // which would require trimming or splitting the region.  Then cleanup.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG( 0x800, 0x1000), .add = false, .incomplete = false, .cnt = 1, .res = false },
    { .reg = REG(0x1800, 0x1000), .add = false, .incomplete = false, .cnt = 1, .res = false },
    { .reg = REG( 0x800, 0x2000), .add = false, .incomplete = false, .cnt = 1, .res = false },
    { .reg = REG(0x1000, 0x1000), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // allow_incomplete == true
    // Tests where incomplete subtraction is allowed.  Start by repeating the
    // tests for allow_incomplete = false where success was expected.  These
    // should work too.

    // Add a region, then subtract it out.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1000, 0x1000), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // Add a region, then trim the front of it.  Finally, cleanup by removing
    // the specific regions which should be left.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = true,  .cnt = 1, .res = true  },
    { .reg = REG(0x1800,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then trim the back of it.  Then cleanup.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1800,  0x800), .add = false, .incomplete = true,  .cnt = 1, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then punch a hole in the middle of it. then cleanup.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1600,  0x400), .add = false, .incomplete = true,  .cnt = 2, .res = true  },
    { .reg = REG(0x1000,  0x600), .add = false, .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1A00,  0x600), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Now try scenarios which only work when allow_incomplete is true.
    // Add a region, then trim the front.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG( 0x800, 0x1000), .add = false, .incomplete = true,  .cnt = 1, .res = true  },
    { .reg = REG(0x1800,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then trim the back.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x1800, 0x1000), .add = false, .incomplete = true,  .cnt = 1, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Add a region, then consume the whole thing.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG( 0x800, 0x2000), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // Add a bunch of separate regions, then consume them all using a subtract
    // which lines up perfectly with the begining and the end of the regions.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG(0x1000, 0xA000), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // Same as before, but this time, trim past the start
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG( 0x800, 0xA800), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // Same as before, but this time, trim past the end
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG(0x1000, 0xA800), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // Same as before, but this time, trim past both ends
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG( 0x800, 0xB000), .add = false, .incomplete = true,  .cnt = 0, .res = true  },

    // Same as before, but this time, don't consume all of the first region.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG(0x1800, 0x9800), .add = false, .incomplete = true,  .cnt = 1, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Same as before, but this time, don't consume all of the last region.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG(0x1000, 0x8800), .add = false, .incomplete = true,  .cnt = 1, .res = true  },
    { .reg = REG(0x9800,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },

    // Same as before, but this time, don't consume all of the first or last regions.
    { .reg = REG(0x1000, 0x1000), .add = true,  .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x3000, 0x1000), .add = true,  .incomplete = false, .cnt = 2, .res = true  },
    { .reg = REG(0x5000, 0x1000), .add = true,  .incomplete = false, .cnt = 3, .res = true  },
    { .reg = REG(0x7000, 0x1000), .add = true,  .incomplete = false, .cnt = 4, .res = true  },
    { .reg = REG(0x9000, 0x1000), .add = true,  .incomplete = false, .cnt = 5, .res = true  },
    { .reg = REG(0x1800, 0x8000), .add = false, .incomplete = true,  .cnt = 2, .res = true  },
    { .reg = REG(0x1000,  0x800), .add = false, .incomplete = false, .cnt = 1, .res = true  },
    { .reg = REG(0x9800,  0x800), .add = false, .incomplete = false, .cnt = 0, .res = true  },
};

#undef REG
