// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/pow2_range_allocator.h>
#include <lib/unittest/unittest.h>

namespace {

bool init_free_test() {
  BEGIN_TEST;

  // The max_alloc_size must be a power of two. Test all those first.
  for (uint32_t size = 1; size != 0; size <<= 1) {
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(size), ZX_OK);
    p2ra.Free();
  }

  // Non-power of two sizes should fail.
  for (uint32_t size : {0, 3, 7, 11, 12, 48}) {
    Pow2RangeAllocator p2ra = {};
    EXPECT_EQ(p2ra.Init(size), ZX_ERR_INVALID_ARGS);
  }

  END_TEST;
}

bool add_range_test() {
  BEGIN_TEST;

  {
    // Adding a range that wraps a uint32_t should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(1u << 31, 1u << 31), ZX_ERR_INVALID_ARGS);
    p2ra.Free();
  }

  {
    // Adding a zero-length range should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(32, 0), ZX_ERR_INVALID_ARGS);
    p2ra.Free();
  }

  {
    // Adding the same range twice should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 32), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 32), ZX_ERR_ALREADY_EXISTS);
    p2ra.Free();
  }

  {
    // Adding a subrange of an already-added range should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 32), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(32, 16), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 16), ZX_ERR_ALREADY_EXISTS);
    p2ra.Free();
  }

  {
    // Adding a super-range of an already range should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 16), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 32), ZX_ERR_ALREADY_EXISTS);
    p2ra.Free();
  }

  {
    // Adding adjacent ranges should succeed.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 16), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(16, 16), ZX_OK);
    p2ra.Free();
  }

  {
    // Adding a range larger than the initialized size should succeed.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 128), ZX_OK);
    p2ra.Free();
  }

  {
    // Adding a bunch of ranges should succeed.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(128), ZX_OK);
    for (uint32_t size = 1u; size < 128; size *= 2) {
      EXPECT_EQ(p2ra.AddRange(size, size), ZX_OK);
    }
    p2ra.Free();
  }

  END_TEST;
}

bool allocate_range_test() {
  BEGIN_TEST;

  {
    // Allocating with a null range pointer should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 16), ZX_OK);
    EXPECT_EQ(p2ra.AllocateRange(4, nullptr), ZX_ERR_INVALID_ARGS);
    p2ra.Free();
  }

  {
    // Allocating a range with a non-power-of-2 length should fail.
    Pow2RangeAllocator p2ra;
    EXPECT_EQ(p2ra.Init(64), ZX_OK);
    EXPECT_EQ(p2ra.AddRange(0, 64), ZX_OK);
    uint32_t range_start;
    EXPECT_EQ(p2ra.AllocateRange(0, &range_start), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(p2ra.AllocateRange(3, &range_start), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(p2ra.AllocateRange(3, &range_start), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(p2ra.AllocateRange(7, &range_start), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(p2ra.AllocateRange(48, &range_start), ZX_ERR_INVALID_ARGS);
    p2ra.Free();
  }

  {
    // Ranges should be distinct.
    for (uint32_t range_length : {1, 4, 16}) {
      const uint32_t number_of_ranges = 64;
      const uint32_t total_size = number_of_ranges * range_length;
      Pow2RangeAllocator p2ra;
      EXPECT_EQ(p2ra.Init(total_size), ZX_OK);
      EXPECT_EQ(p2ra.AddRange(0, total_size), ZX_OK);
      uint64_t mask = 0u;
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        uint32_t range_start;
        EXPECT_EQ(p2ra.AllocateRange(range_length, &range_start), ZX_OK);
        EXPECT_LT(range_start, total_size);
        uint64_t bit = (1ull << (range_start / range_length));
        EXPECT_EQ(mask & bit, 0u);
        mask |= bit;
      }
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        p2ra.FreeRange(range_length * idx, range_length);
      }
      p2ra.Free();
    }
  }

  {
    // We should be able to allocate an entire range, free a hole, and
    // reallocate in the same place.
    for (uint32_t range_length : {1, 4, 16}) {
      const uint32_t number_of_ranges = 64;
      const uint32_t total_size = number_of_ranges * range_length;
      Pow2RangeAllocator p2ra;
      EXPECT_EQ(p2ra.Init(total_size), ZX_OK);
      EXPECT_EQ(p2ra.AddRange(0, total_size), ZX_OK);
      uint64_t mask = 0u;
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        uint32_t range_start;
        EXPECT_EQ(p2ra.AllocateRange(range_length, &range_start), ZX_OK);
        EXPECT_LT(range_start, total_size);
        uint64_t bit = (1ull << (range_start / range_length));
        EXPECT_EQ(mask & bit, 0u);
        mask |= bit;
      }
      // Actually make and refill the holes.
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        p2ra.FreeRange(range_length * idx, range_length);
        uint32_t range_start;
        EXPECT_EQ(p2ra.AllocateRange(range_length, &range_start), ZX_OK);
        EXPECT_EQ(range_start, idx * range_length);
      }
      // Clean up.
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        p2ra.FreeRange(range_length * idx, range_length);
      }
      p2ra.Free();
    }
  }

  {
    // We should be able to allocate an entire range, free some
    // continguous small holes, and reallocate larger ranges in the
    // same place.
    for (uint32_t range_length : {2, 4, 8}) {
      for (uint32_t ranges_per_large_range : {2, 4, 8}) {
        const uint32_t large_range_length = ranges_per_large_range * range_length;
        const uint32_t number_of_ranges = 64;
        const uint32_t number_of_large_ranges = number_of_ranges / ranges_per_large_range;
        const uint32_t total_size = number_of_ranges * range_length;
        Pow2RangeAllocator p2ra;
        EXPECT_EQ(p2ra.Init(total_size), ZX_OK);
        EXPECT_EQ(p2ra.AddRange(0, total_size), ZX_OK);
        uint64_t mask = 0u;
        for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
          uint32_t range_start;
          EXPECT_EQ(p2ra.AllocateRange(range_length, &range_start), ZX_OK);
          EXPECT_LT(range_start, total_size);
          uint64_t bit = (1ull << (range_start / range_length));
          EXPECT_EQ(mask & bit, 0u);
          mask |= bit;
        }
        // Actually make and refill the holes.
        for (uint32_t idx = 0; idx < number_of_large_ranges; ++idx) {
          for (uint32_t subidx = 0; subidx < ranges_per_large_range; ++subidx) {
            uint32_t range_start = ((idx * ranges_per_large_range) + subidx) * range_length;
            p2ra.FreeRange(range_start, range_length);
          }
          uint32_t large_range_start;
          EXPECT_EQ(p2ra.AllocateRange(large_range_length, &large_range_start), ZX_OK);
          EXPECT_EQ(large_range_start, idx * large_range_length);
        }
        // Clean up.
        for (uint32_t idx = 0; idx < number_of_large_ranges; ++idx) {
          p2ra.FreeRange(large_range_length * idx, large_range_length);
        }
        p2ra.Free();
      }
    }
  }

  {
    // Fragmentation should be able to prevent us from allocating.
    for (uint32_t range_length : {1, 4, 16}) {
      const uint32_t number_of_ranges = sizeof(uint64_t);
      const uint32_t total_size = number_of_ranges * range_length;
      const uint32_t stride = 4;
      Pow2RangeAllocator p2ra;
      EXPECT_EQ(p2ra.Init(total_size), ZX_OK);
      EXPECT_EQ(p2ra.AddRange(0, total_size), ZX_OK);
      uint64_t mask = 0u;
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        uint32_t range_start;
        EXPECT_EQ(p2ra.AllocateRange(range_length, &range_start), ZX_OK);
        EXPECT_LT(range_start, total_size);
        uint64_t bit = (1ull << (range_start / range_length));
        EXPECT_EQ(mask & bit, 0u);
        mask |= bit;
      }
      // Leave every 4th allocated, and free the rest.
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        if (idx % stride == 0) {
          continue;
        }
        p2ra.FreeRange(range_length * idx, range_length);
      }
      // It should now be impossible to allocate a 4-times larger range.
      uint32_t range_start;
      EXPECT_EQ(p2ra.AllocateRange(stride * range_length, &range_start), ZX_ERR_NO_RESOURCES);
      // Clean up the remaining gaps.
      for (uint32_t idx = 0; idx < number_of_ranges; idx += stride) {
        p2ra.FreeRange(range_length * idx, range_length);
      }
      p2ra.Free();
    }
  }

  {
    // If we initialize a small size, and then add a larger range, we
    // should be able to spread out over the larger range.
    for (uint32_t range_length : {1, 4, 16}) {
      // This time, the maximum size of an allocation is less than the
      // full space we will add.
      const uint32_t sparseness = 2;
      const uint32_t number_of_ranges = 64 / sparseness;
      const uint32_t total_size = number_of_ranges * range_length;
      const uint32_t upper_bound = 2 * total_size;
      Pow2RangeAllocator p2ra;
      EXPECT_EQ(p2ra.Init(total_size), ZX_OK);
      // The range is larger than the initialized size
      EXPECT_EQ(p2ra.AddRange(0, 2 * total_size), ZX_OK);
      // Allocate as much as we can.
      uint64_t mask = 0;
      // Track in particular if any of our ranges are outside [0, total_size).
      bool got_up_high = false;
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        uint32_t range_start;
        EXPECT_EQ(p2ra.AllocateRange(range_length, &range_start), ZX_OK);
        // Note that the upper bound here is bigger, by design.
        EXPECT_LT(range_start, upper_bound);
        uint64_t bit = (1ull << (range_start / range_length));
        EXPECT_EQ(mask & bit, 0u);
        mask |= bit;
        if (range_start >= total_size) {
          got_up_high = true;
        }
      }
      // If we already set some high ranges, we've proved our
      // point. Otherwise, we only have a pile of contiguous
      // ranges. So can free any two non-contiguous ranges, and
      // allocate a slightly bigger one. That slightly bigger one will
      // be forced to fit higher up.
      if (!got_up_high) {
        // Double check our logic. If we never got allocated a high range, then mask better be all
        // low bits.
        EXPECT_EQ(mask, 0xffffffffull);
        // Free a non-contiguous pair of small ranges (at spots 0 and 2).
        p2ra.FreeRange(0, range_length);
        p2ra.FreeRange(2 * range_length, range_length);
        // Now we should be allocate a range twice as big.
        uint32_t range_start;
        EXPECT_EQ(p2ra.AllocateRange(2 * range_length, &range_start), ZX_OK);
        // And it must be somewhere after |total_size|.
        EXPECT_GE(range_start, total_size);
        // Let the big one go now.
        p2ra.FreeRange(range_start, 2 * range_length);
      }
      // Clean up.
      for (uint32_t idx = 0; idx < number_of_ranges; ++idx) {
        if (!got_up_high && (idx == 0 || idx == 2)) {
          // We freed these just above, already.
          continue;
        }
        p2ra.FreeRange(range_length * idx, range_length);
      }
      p2ra.Free();
    }
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(pow2_range_allocator_tests)
UNITTEST("InitFree", init_free_test)
UNITTEST("AddRange", add_range_test)
UNITTEST("AllocateRange", allocate_range_test)
UNITTEST_END_TESTCASE(pow2_range_allocator_tests, "Pow2RangeAllocator", "Pow2RangeAllocator tests")

}  // namespace
