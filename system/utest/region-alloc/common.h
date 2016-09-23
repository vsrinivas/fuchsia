// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <region-alloc/region-alloc.h>
#include <stddef.h>

// Constants and common tables used by both the C and C++ API tests.

#define REGION_POOL_SLAB_SIZE (4u << 10)
#define REGION_POOL_MAX_SIZE  (REGION_POOL_SLAB_SIZE << 1)
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
    { .size = 0x00000000, .align = 0x00000001, .res = ERR_INVALID_ARGS, 0 },  // bad size
    { .size = 0x00000001, .align = 0x00000000, .res = ERR_INVALID_ARGS, 0 },  // bad align
    { .size = 0x00000001, .align = 0x00001001, .res = ERR_INVALID_ARGS, 0 },  // bad align

    // Initially unsatisfiable
    { .size = 0x10000000, .align = 0x00000001, .res = ERR_NOT_FOUND, 0 },  // too large
    { .size = 0x00005000, .align = 0x10000000, .res = ERR_NOT_FOUND, 0 },  // Cannot align

    // Should succeed, all pulled from first chunk
    { .size = (1 <<  0), .align = (1 <<  1), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  1), .align = (1 <<  2), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  2), .align = (1 <<  3), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  3), .align = (1 <<  4), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  4), .align = (1 <<  5), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  5), .align = (1 <<  6), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  6), .align = (1 <<  7), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  7), .align = (1 <<  8), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  8), .align = (1 <<  9), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  9), .align = (1 << 10), .res = NO_ERROR, .region = 0 },
    { .size = (1 << 10), .align = (1 << 11), .res = NO_ERROR, .region = 0 },

    // Perform some allocations which are large enough that they can only be
    // satisfied with results from region 1.  Exercise the various range
    // splitting cases.
    { .size = (4 << 10), .align = (4 << 10), .res = NO_ERROR, .region = 1 }, // front of region 1
    { .size = (4 << 10), .align = (4 << 11), .res = NO_ERROR, .region = 1 }, // middle of region 1
    { .size = 0xfc000,   .align = (4 << 12), .res = NO_ERROR, .region = 1 }, // back of region 1

    // Repeat the small allocation pass again.  Because of the alignment
    // restrictions, the first pass should have fragmented the first region.
    // This pass should soak up those fragments.
    { .size = (3),       .align = (1 <<  0), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  1), .align = (1 <<  1), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  2), .align = (1 <<  2), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  3), .align = (1 <<  3), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  4), .align = (1 <<  4), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  5), .align = (1 <<  5), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  6), .align = (1 <<  6), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  7), .align = (1 <<  7), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  8), .align = (1 <<  8), .res = NO_ERROR, .region = 0 },
    { .size = (1 <<  9), .align = (1 <<  9), .res = NO_ERROR, .region = 0 },
    { .size = (1 << 10), .align = (1 << 10), .res = NO_ERROR, .region = 0 },

    // Region 0 should be exhausted at this point.  Asking for even one more
    // byte should give us an allocation from from region 1.
    { .size = 1, .align = 1, .res = NO_ERROR, .region = 1 },

    // All that should be left in the pool is a 4k region and a 4k - 1 byte
    // region.  Ask for two 4k regions with arbitrary alignment.  The first
    // request should succeed while the second request should fail.
    { .size = (4 << 10), .align = 1, .res = NO_ERROR, .region = 1 },
    { .size = (4 << 10), .align = 1, .res = ERR_NOT_FOUND, 0 },

    // Finally, soak up the last of the space with a 0xFFF byte allocation.
    // Afterwards, we should be unable to allocate even a single byte
    { .size = 0xFFF, .align = 1, .res = NO_ERROR, .region = 1 },
    { .size = 1,     .align = 1, .res = ERR_NOT_FOUND, 0 },
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
    { .req = { .base = 0x0000000000000000, .size = 0x00 }, .res = ERR_INVALID_ARGS },  // 0 size
    { .req = { .base = 0xffffffffffffffff, .size = 0x01 }, .res = ERR_INVALID_ARGS },  // wraps
    { .req = { .base = 0xfffffffffffffff0, .size = 0x20 }, .res = ERR_INVALID_ARGS },  // wraps

    // Bad requests
    { .req = { .base = 0x0800, .size =   0x1 }, .res = ERR_NOT_FOUND },  // total miss
    { .req = { .base = 0x0fff, .size = 0x100 }, .res = ERR_NOT_FOUND },  // clips the front
    { .req = { .base = 0x1f01, .size = 0x100 }, .res = ERR_NOT_FOUND },  // clips the back
    { .req = { .base = 0x2000, .size =   0x1 }, .res = ERR_NOT_FOUND },  // total miss

    // Good requests
    { .req = { .base = 0x1000, .size = 0x100 }, .res = NO_ERROR },  // front of range.
    { .req = { .base = 0x1f00, .size = 0x100 }, .res = NO_ERROR },  // back of range.
    { .req = { .base = 0x1700, .size = 0x200 }, .res = NO_ERROR },  // middle of range.

    // Requests which would have been good initially, but are bad now.
    { .req = { .base = 0x1000, .size = 0x100 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x1080, .size =  0x80 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x10ff, .size =   0x1 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x10ff, .size = 0x100 }, .res = ERR_NOT_FOUND },

    { .req = { .base = 0x1f00, .size = 0x100 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x1e01, .size = 0x100 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x1e81, .size =  0x80 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x1eff, .size =   0x2 }, .res = ERR_NOT_FOUND },

    { .req = { .base = 0x1800, .size = 0x100 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x1880, .size = 0x100 }, .res = ERR_NOT_FOUND },
    { .req = { .base = 0x1780, .size = 0x100 }, .res = ERR_NOT_FOUND },

    // Soak up the remaining regions.  There should be 2 left.
    { .req = { .base = 0x1100, .size = 0x600 }, .res = NO_ERROR },
    { .req = { .base = 0x1900, .size = 0x600 }, .res = NO_ERROR },
};
