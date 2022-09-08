// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/memalloc/pool.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/limits.h>

#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "test.h"

namespace {

using namespace std::string_view_literals;

using memalloc::Pool;
using memalloc::Range;
using memalloc::Type;

constexpr uint64_t kChunkSize = Pool::kBookkeepingChunkSize;
constexpr uint64_t kDefaultAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
constexpr uint64_t kDefaultMinAddr = 0;
constexpr uint64_t kDefaultMaxAddr = std::numeric_limits<uintptr_t>::max();
constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

constexpr const char* kPrintOutPrefix = "PREFIX";

constexpr std::string_view kEmptyPrintOut =
    R"""(PREFIX: | Physical memory range                    | Size    | Type
)""";

void TestPoolInit(Pool& pool, cpp20::span<Range> input, std::optional<uint64_t> min_addr = {},
                  std::optional<uint64_t> max_addr = {}, bool init_error = false) {
  auto status = pool.Init(std::array{input}, min_addr.value_or(kDefaultMinAddr),
                          max_addr.value_or(kDefaultMaxAddr));
  if (init_error) {
    ASSERT_TRUE(status.is_error());
    return;
  }
  ASSERT_FALSE(status.is_error());
}

void TestPoolContents(const Pool& pool, cpp20::span<const Range> expected) {
  EXPECT_EQ(expected.size(), pool.size());
  std::vector<const Range> actual(pool.begin(), pool.end());
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));
}

void TestPoolPrintOut(const Pool& pool, const char* prefix, std::string_view expected) {
  constexpr size_t kPrintOutSizeMax = 0x400;

  FILE* f = tmpfile();
  auto cleanup = fit::defer([f]() { fclose(f); });

  pool.PrintMemoryRanges(prefix, f);
  rewind(f);

  char buff[kPrintOutSizeMax];
  size_t n = fread(buff, 1, kPrintOutSizeMax, f);
  ASSERT_EQ(0, ferror(f)) << "failed to read file: " << strerror(errno);

  std::string_view actual{buff, n};
  EXPECT_EQ(expected, actual);
}

void TestPoolAllocation(Pool& pool, Type type, uint64_t size, uint64_t alignment,
                        std::optional<uint64_t> min_addr = {},
                        std::optional<uint64_t> max_addr = {}, bool alloc_error = false) {
  auto result = pool.Allocate(type, size, alignment, min_addr, max_addr);
  if (alloc_error) {
    ASSERT_TRUE(result.is_error());
    return;
  }
  ASSERT_FALSE(result.is_error());

  // The resulting range should now be contained in one of the tracked ranges.
  const Range contained = {
      .addr = std::move(result).value(),
      .size = size,
      .type = type,
  };
  auto is_contained = [&pool](const Range& subrange) -> bool {
    for (const Range& range : pool) {
      if (range.addr <= subrange.addr && subrange.end() <= range.end()) {
        return range.type == subrange.type;
      }
    }
    return false;
  };
  EXPECT_TRUE(is_contained(contained));
}

void TestPoolFreeing(Pool& pool, uint64_t addr, uint64_t size, bool free_error = false) {
  // Returns the tracked type of a single address, if any.
  //
  // Asserting that a range is contained within the union of a connected set
  // of subranges is a bit complicated; accordingly, so assert below on the
  // weaker proposition that inclusive endpoints are tracked (and with expected
  // types).
  auto tracked_type = [&pool](uint64_t addr) -> std::optional<Type> {
    for (const Range& range : pool) {
      if (range.addr <= addr && addr < range.end()) {
        return range.type;
      }
    }
    return std::nullopt;
  };

  EXPECT_TRUE(tracked_type(addr));
  if (size) {
    EXPECT_TRUE(tracked_type(addr + size - 1));
  }

  auto result = pool.Free(addr, size);
  if (free_error) {
    ASSERT_TRUE(result.is_error());
    return;
  }
  ASSERT_FALSE(result.is_error());

  EXPECT_EQ(Type::kFreeRam, tracked_type(addr));
  if (size) {
    EXPECT_EQ(Type::kFreeRam, tracked_type(addr + size - 1));
  }
}

void TestPoolFreeRamSubrangeUpdating(Pool& pool, Type type, uint64_t addr, uint64_t size,
                                     bool alloc_error = false) {
  auto status = pool.UpdateFreeRamSubranges(type, addr, size);
  if (alloc_error) {
    EXPECT_TRUE(status.is_error());
    return;
  }
  EXPECT_FALSE(status.is_error());
}

void TestPoolResizing(Pool& pool, const Range& original, uint64_t new_size, uint64_t min_alignment,
                      std::optional<uint64_t> expected) {
  auto status = pool.Resize(original, new_size, min_alignment);
  if (!expected) {
    EXPECT_TRUE(status.is_error());
    return;
  }
  ASSERT_FALSE(status.is_error());
  EXPECT_EQ(*expected, status.value());
}

// Fills up a pool with two-byte allocations of varying types until its
// bookkeeping space is used up.
void Oom(Pool& pool) {
  // Start just after kPoolBookkeeping, to ensure we don't try to allocate a bad
  // type.
  uint64_t type_val = static_cast<uint64_t>(Type::kPoolBookkeeping) + 1;
  bool failure = false;
  while (!failure && type_val < kUint64Max) {
    Type type = static_cast<Type>(type_val++);
    failure = pool.Allocate(type, 2, 1).is_error();
  }
  ASSERT_NE(type_val, kUint64Max);  // This should never happen.
}

TEST(MemallocPoolTests, NoInputMemory) {
  PoolContext ctx;

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {}, {}, {}, /*init_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
  ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
}

TEST(MemallocPoolTests, NoRam) {
  PoolContext ctx;
  Range ranges[] = {
      // reserved: [0, kChunkSize)
      {
          .addr = 0,
          .size = kChunkSize,
          .type = Type::kReserved,
      },
      // peripheral: [kChunkSize, 2*kChunkSize)
      {
          .addr = kChunkSize,
          .size = kChunkSize,
          .type = Type::kPeripheral,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, {}, {}, /*init_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
  ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
}

TEST(MemallocPoolTests, TooLittleRam) {
  {
    PoolContext ctx;
    Range ranges[] = {
        // RAM: [0, kChunkSize - 1)
        {
            .addr = 0,
            .size = kChunkSize - 1,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, {}, {}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // reserved: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // RAM: [kChunkSize, kChunkSize/2)
        {
            .addr = kChunkSize,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // reserved: [kChunkSize/2, 3*kChunkSize/4)
        {
            .addr = kChunkSize / 2,
            .size = kChunkSize / 4,
            .type = Type::kReserved,
        },
        // RAM: [3*kChunkSize/4, 7*kChunkSize/8)
        {
            .addr = 3 * kChunkSize / 4,
            .size = kChunkSize / 8,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, {}, {}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }
}

TEST(MemallocPoolTests, Bookkeeping) {
  {
    PoolContext ctx;
    Range ranges[] = {
        // RAM: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000001000) |      4K | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // RAM: [0, kChunkSize)
        {
            .addr = 0,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000001000) |      4K | bookkeeping
PREFIX: | [0x0000000000001000, 0x0000000000002000) |      4K | free RAM
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // peripheral: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPeripheral,
        },
        // RAM: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range expected[] = {
        // peripheral: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPeripheral,
        },
        // bookkeeping: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000001000) |      4K | peripheral
PREFIX: | [0x0000000000001000, 0x0000000000002000) |      4K | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // RAM: [kChunkSize/2, 2*kChunkSize)
        {
            .addr = kChunkSize / 2,
            .size = 3 * kChunkSize / 2,
            .type = Type::kFreeRam,
        },
    };

    const Range expected[] = {
        // RAM: [kChunkSize/2, kChunkSize)
        {
            .addr = kChunkSize / 2,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // bookkeeping: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000800, 0x0000000000001000) |      2K | free RAM
PREFIX: | [0x0000000000001000, 0x0000000000002000) |      4K | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // RAM: [0, 2*kChunkSize)
        {
            .addr = 0,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // peripheral: [kChunkSize/2, 3*kChunkSize/2)
        {
            .addr = kChunkSize / 2,
            .size = kChunkSize,
            .type = Type::kPeripheral,
        },
        // RAM: [2*kChunkSize, 3*kChunkSize)
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range expected[] = {
        // RAM: [0. kChunkSize/2)
        {
            .addr = 0,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // peripheral: [kChunkSize/2, 3*kChunkSize/2)
        {
            .addr = kChunkSize / 2,
            .size = kChunkSize,
            .type = Type::kPeripheral,
        },
        // RAM: [3*kChunkSize/2. 2*kChunkSize)
        {
            .addr = 3 * kChunkSize / 2,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // bookkeeping: [2*kChunkSize, 3*kChunkSize)
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000000800) |      2K | free RAM
PREFIX: | [0x0000000000000800, 0x0000000000001800) |      4K | peripheral
PREFIX: | [0x0000000000001800, 0x0000000000002000) |      2K | free RAM
PREFIX: | [0x0000000000002000, 0x0000000000003000) |      4K | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }
}

TEST(MemallocPoolTests, ReservedRangesAreNotExplicitlyTracked) {
  {
    PoolContext ctx;
    Range ranges[] = {
        // reserved: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // free RAM: [0, 3 * kChunkSize)
        {
            .addr = 0,
            .size = 3 * kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range expected[] = {
        // bookkeeping: [kChunksize,  2 * kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [2 * kChunksize,  3 * kChunkSize)
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000001000, 0x0000000000002000) |      4K | bookkeeping
PREFIX: | [0x0000000000002000, 0x0000000000003000) |      4K | free RAM
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // free RAM: [0, 3 * kChunkSize)
        {
            .addr = 0,
            .size = 3 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // reserved: [kChunkSize, 2* kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
    };

    const Range expected[] = {
        // bookkeeping: [0, kChunksize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [2 * kChunksize,  3 * kChunkSize)
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000001000) |      4K | bookkeeping
PREFIX: | [0x0000000000002000, 0x0000000000003000) |      4K | free RAM
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    Range ranges[] = {
        // free RAM: [0, 3 * kChunkSize)
        {
            .addr = 0,
            .size = 3 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // reserved: [2 * kChunkSize, 3 * kChunkSize)
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
    };

    const Range expected[] = {
        // bookkeeping: [0, kChunksize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [kChunksize,  2 * kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000001000) |      4K | bookkeeping
PREFIX: | [0x0000000000001000, 0x0000000000002000) |      4K | free RAM
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }
}

TEST(MemallocPoolTests, GetContainingRange) {
  PoolContext ctx;
  Range ranges[] = {
      // RAM: [0, 3*kChunkSize)
      {
          .addr = 0,
          .size = 3 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  const Range expected[] = {
      // bookkeeping: [0, kChunkSize)
      {
          .addr = 0,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // RAM: [kChunkSize, 3*kChunkSize)
      {
          .addr = kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  EXPECT_EQ(expected[0], *ctx.pool.GetContainingRange(kDefaultMinAddr));
  EXPECT_EQ(expected[0], *ctx.pool.GetContainingRange(kChunkSize - 1));
  EXPECT_EQ(expected[1], *ctx.pool.GetContainingRange(kChunkSize));
  EXPECT_EQ(expected[1], *ctx.pool.GetContainingRange(2 * kChunkSize));
  EXPECT_EQ(expected[1], *ctx.pool.GetContainingRange(3 * kChunkSize - 1));
  EXPECT_FALSE(ctx.pool.GetContainingRange(3 * kChunkSize));
}

TEST(MemallocPoolTests, DefaultAllocationBounds) {
  Range ranges[] = {
      // free RAM: [0, 100*kChunkSize)
      {
          .addr = 0,
          .size = 100 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  {
    // A sufficiently large minimum address leaves no room for bookkeeping.
    PoolContext ctx;
    ASSERT_NO_FATAL_FAILURE(
        TestPoolInit(ctx.pool, {ranges}, 100 * kChunkSize, 0, /*init_error=*/true));
  }

  {
    // A sufficiently small maximum address leaves no room for bookkeeping.
    PoolContext ctx;
    ASSERT_NO_FATAL_FAILURE(
        TestPoolInit(ctx.pool, {ranges}, 0, kChunkSize / 2, /*init_error=*/true));
  }

  {
    // Default bounds correspond to [10 * kChunkSize, 50 * kChunkSize).
    const Range after_init[] = {
        // free RAM: [0, 10*kChunkSize)
        {
            .addr = 0,
            .size = 10 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // bookkeeping: [10*kChunkSize, 11*kChunkSize)
        {
            .addr = 10 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // free RAM: [11*kChunkSize, 100*kChunkSize)
        {
            .addr = 11 * kChunkSize,
            .size = 89 * kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    PoolContext ctx;
    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, 10 * kChunkSize, 50 * kChunkSize));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {after_init}));

    // Despite there being 89 chunks available past the bookkeeping, the pool's
    // default address bound means that only 39 of them are accessible unless
    // overridden.
    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 40 * kChunkSize,
                                               kDefaultAlignment, {}, {}, /*alloc_error=*/true));

    // Though we could override that default now to allocate the remaining chunks.
    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 89 * kChunkSize,
                                               kDefaultAlignment, {}, 100 * kChunkSize));

    // Similarly, despite there being 10 chunks before the bookkeeping, they
    // are inaccessible given the default lower bound, unless overridden.
    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 10 * kChunkSize,
                                               kDefaultAlignment, {}, {}, /*alloc_error=*/true));

    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 10 * kChunkSize,
                                               kDefaultAlignment, 0, {}));
  }
}

TEST(MemallocPoolTests, NoResourcesAllocation) {
  PoolContext ctx;
  Range ranges[] = {
      // free RAM: [kChunkSize, 3*kChunkSize)
      {
          .addr = kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const Range expected[] = {
      // bookkeeping: [kChunkSize, 2*kChunkSize)
      {
          .addr = kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [2*kChunkSize, 3*kChunkSize)
      {
          .addr = 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  // Requested size is too big:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 2 * kChunkSize,
                                             kDefaultAlignment, {}, {},
                                             /*alloc_error=*/true));
  // Requested alignment is too big:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kChunkSize << 2, {}, {},
                                             /*alloc_error=*/true));
  // Requested min address is too big:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kDefaultAlignment, 2 * kChunkSize + 1, {},
                                             /*alloc_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kDefaultAlignment, 3 * kChunkSize, {},
                                             /*alloc_error=*/true));

  // Requested max address is too small:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kDefaultAlignment, {}, 3 * kChunkSize - 2,
                                             /*alloc_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kDefaultAlignment, {}, 2 * kChunkSize,
                                             /*alloc_error=*/true));

  // Nothing should have changed.
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
}

TEST(MemallocPoolTests, AllocationWithEqualBounds) {
  Range ranges[] = {
      // free RAM: [kChunkSize, 3*kChunkSize)
      {
          .addr = kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  // Equal bounds passed to Init().
  {
    PoolContext ctx;
    ASSERT_NO_FATAL_FAILURE(
        TestPoolInit(ctx.pool, {ranges}, kChunkSize, kChunkSize, /*init_error=*/true));
  }

  // Equal bounds passed to Allocate().
  {
    const Range expected[] = {
        // bookkeeping: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // free RAM: [2*kChunkSize, 3*kChunkSize)
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    PoolContext ctx;
    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

    // An allocation with size > 1 should fail.
    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 2,
                                               kDefaultAlignment, 2 * kChunkSize, 2 * kChunkSize,
                                               /*alloc_error=*/true));

    // But an allocation with size == 1 should succeed.
    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 1,
                                               kDefaultAlignment, 2 * kChunkSize, 2 * kChunkSize));
  }
}

TEST(MemallocPoolTests, ExhaustiveAllocation) {
  Range ranges[] = {
      // free RAM: [kChunkSize, 3*kChunkSize)
      {
          .addr = kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const Range expected_before[] = {
      // bookkeeping: [kChunkSize, 2*kChunkSize)
      {
          .addr = kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [2*kChunkSize, 3*kChunkSize)
      {
          .addr = 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const Range expected_after[] = {
      // bookkeeping: [kChunkSize, 2*kChunkSize)
      {
          .addr = kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [2*kChunkSize, 3*kChunkSize)
      {
          .addr = 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolTestPayload,
      },
  };

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    ASSERT_NO_FATAL_FAILURE(
        TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize, kDefaultAlignment));

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    for (size_t i = 0; i < 2; ++i) {
      ASSERT_NO_FATAL_FAILURE(
          TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize / 2, kDefaultAlignment));
    }

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    for (size_t i = 0; i < 4; ++i) {
      ASSERT_NO_FATAL_FAILURE(
          TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize / 4, kDefaultAlignment));
    }

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    for (size_t i = 0; i < kChunkSize; ++i) {
      ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 1, 1));
    }

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }
}

TEST(MemallocPoolTests, Freeing) {
  PoolContext ctx;
  Range ranges[] = {
      // RAM: [0, 2*kChunkSize)
      {
          .addr = 0,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
      // data ZBI: [2*kChunkSize, 3*kChunkSize)
      {
          .addr = 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kDataZbi,
      },
  };

  const Range expected[] = {
      // bookkeeping: [0, kChunkSize)
      {
          .addr = 0,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [kChunkSize, 2*kChunkSize)
      {
          .addr = kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
      // data ZBI: [2*kChunkSize, 3*kChunkSize)
      {
          .addr = 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kDataZbi,
      },
  };

  const Range expected_after[] = {
      // bookkeeping: [0, kChunkSize)
      {
          .addr = 0,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [kChunkSize, 3*kChunkSize)
      {
          .addr = kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  // A subrange of extended type passed to Init() can be freed.
  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, 2 * kChunkSize, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, 5 * kChunkSize / 2, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  // Double-frees should be no-ops.
  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, kChunkSize, kChunkSize));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, kChunkSize, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, 3 * kChunkSize / 2, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, 2 * kChunkSize, kChunkSize));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
}

TEST(MemallocPoolTests, FreedAllocations) {
  PoolContext ctx;
  Range ranges[] = {
      // free RAM: [0, 2*kChunkSize)
      {
          .addr = 0,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const Range expected[] = {
      // bookkeeping: [0, kChunkSize)
      {
          .addr = 0,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [kChunkSize, 2*kChunkSize)
      {
          .addr = kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  constexpr auto allocate_then_free = [](Pool& pool, uint64_t size) {
    auto result = pool.Allocate(Type::kPoolTestPayload, size, 1);
    ASSERT_FALSE(result.is_error());
    uint64_t addr = std::move(result).value();
    EXPECT_FALSE(pool.Free(addr, size).is_error());
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, 1));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, 4));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, 8));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, 16));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  ASSERT_NO_FATAL_FAILURE(allocate_then_free(ctx.pool, kChunkSize));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
}

TEST(MemallocPoolTests, FreeRamSubrangeUpdates) {
  PoolContext ctx;
  Range ranges[] = {
      // RAM: [0, 3*kChunkSize)
      {
          .addr = 0,
          .size = 3 * kChunkSize,
          .type = Type::kFreeRam,
      },
      // data ZBI: [3*kChunkSize, 4*kChunkSize)
      {
          .addr = 3 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kDataZbi,
      },
      // RAM: [4*kChunkSize, 5*kChunkSize)
      {
          .addr = 4 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
      // RAM: [5*kChunkSize, 6*kChunkSize)
      {
          .addr = 5 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kPhysKernel,
      },
      // RAM: [6*kChunkSize, 7*kChunkSize)
      {
          .addr = 6 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

  {
    const Range expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [kChunkSize, 3*kChunkSize)
        {
            .addr = kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize)
        {
            .addr = 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // RAM: [4*kChunkSize, 5*kChunkSize)
        {
            .addr = 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize)
        {
            .addr = 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // RAM: [6*kChunkSize, 7*kChunkSize)
        {
            .addr = 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }

  // Updating can happen across an extended type.
  {
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            kDefaultMinAddr, 3 * kChunkSize));

    const Range expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // test payload: [kChunkSize, 3*kChunkSize)
        // Updated.
        {
            .addr = kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize)
        {
            .addr = 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // RAM: [4*kChunkSize, 5*kChunkSize)
        {
            .addr = 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize)
        {
            .addr = 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // RAM: [6*kChunkSize, 7*kChunkSize)
        {
            .addr = 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

    // Weak allocation does not affect extended type ranges, even when there
    // is no free RAM in the provided range.
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            3 * kChunkSize, kChunkSize));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }

  {
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            3 * kChunkSize, 3 * kChunkSize));

    const Range expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // test payload: [kChunkSize, 3*kChunkSize)
        // Updated.
        {
            .addr = kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize)
        {
            .addr = 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // test payload: [4*kChunkSize, 5*kChunkSize)
        {
            // Updated.
            .addr = 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize)
        {
            .addr = 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // RAM: [6*kChunkSize, 7*kChunkSize)
        {
            .addr = 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }

  {
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            kDefaultMinAddr, 7 * kChunkSize));

    const Range expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // test payload: [kChunkSize, 3*kChunkSize)
        // Updated.
        {
            .addr = kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize)
        {
            .addr = 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // test payload: [4*kChunkSize, 5*kChunkSize)
        {
            .addr = 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize)
        {
            .addr = 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // test payload: [6*kChunkSize, 7*kChunkSize)
        {
            // Updated.
            .addr = 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }
}

TEST(MemallocPoolTests, Resizing) {
  constexpr uint64_t kMinAlignment = kChunkSize;

  // kNewSize == kOldSize
  {
    constexpr uint64_t kOldAddr = kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        kRange,
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kOldSize, kMinAlignment, kOldAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));

    // Resizing with a smaller alignment should be a no-opt.
    ASSERT_NO_FATAL_FAILURE(
        TestPoolResizing(ctx.pool, kRange, kOldSize, kMinAlignment / 2, kOldAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize < kOldSize
  {
    constexpr uint64_t kOldAddr = kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = kChunkSize / 2;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        kRange,
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kOldAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kOldAddr + kNewSize,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kOldAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // Room for extension in-place.
  // No coalesced ranges.
  {
    constexpr uint64_t kOldAddr = kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        kRange,
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kOldAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kOldAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // Room for extension in-place.
  // Coalesced range on left.
  {
    constexpr uint64_t kOldAddr = 2 * kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kChunkSize,
            .size = kChunkSize + kOldSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kChunkSize,
            .size = kChunkSize + kNewSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kOldAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // No wiggle room; must reallocate into discontiguous memory.
  // No coalesced ranges.
  {
    constexpr uint64_t kOldAddr = kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;
    constexpr uint64_t kNewAddr = 10 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        kRange,
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kFreeRam,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kNewAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // No wiggle room; must reallocate into discontiguous memory.
  // Coalesced range on left.
  {
    constexpr uint64_t kOldAddr = 2 * kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;
    constexpr uint64_t kNewAddr = 10 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kChunkSize,
            .size = kChunkSize + kOldSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kFreeRam,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kNewAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // No wiggle room; must reallocate into discontiguous memory.
  // Coalesced range on right.
  {
    constexpr uint64_t kOldAddr = kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;
    constexpr uint64_t kNewAddr = 10 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize + kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kFreeRam,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kNewAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // No wiggle room; must reallocate into discontiguous memory.
  // Coalesced ranges on both sides.
  {
    constexpr uint64_t kOldAddr = 2 * kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;
    constexpr uint64_t kNewAddr = 10 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kChunkSize,
            .size = kChunkSize + kOldSize + kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kFreeRam,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kNewAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // Must reallocate into preceding range.
  // No coalesced range on right.
  {
    constexpr uint64_t kOldAddr = 2 * kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 3 * kChunkSize / 2;
    constexpr uint64_t kNewAddr = kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        kRange,
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr + kNewSize,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kNewAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // Must reallocate into preceding range.
  // Coalesced range on right.
  {
    constexpr uint64_t kOldAddr = 2 * kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 3 * kChunkSize / 2;
    constexpr uint64_t kNewAddr = kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        {
            .addr = kOldAddr,
            .size = kOldSize + kChunkSize,
            .type = Type::kPoolTestPayload,
        },
    };

    const Range post_resize[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        {
            .addr = kNewAddr,
            .size = kNewSize,
            .type = Type::kPoolTestPayload,
        },
        {
            .addr = kNewAddr + kNewSize,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        {
            .addr = kOldAddr + kOldSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, kNewAddr));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {post_resize}));
  }

  // kNewSize > kOldSize
  // OOM.
  {
    constexpr uint64_t kOldAddr = kChunkSize;
    constexpr uint64_t kOldSize = kChunkSize;
    constexpr uint64_t kNewSize = 2 * kChunkSize;

    constexpr Range kRange{
        .addr = kOldAddr,
        .size = kOldSize,
        .type = Type::kPoolTestPayload,
    };

    PoolContext ctx;
    Range ranges[] = {
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        kRange,
        {
            .addr = 2 * kChunkSize,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

    ASSERT_NO_FATAL_FAILURE(
        TestPoolResizing(ctx.pool, kRange, kNewSize, kMinAlignment, std::nullopt));
  }
}

TEST(MemallocPoolTests, OutOfMemory) {
  PoolContext ctx;
  Range ranges[] = {
      // free RAM: [0, 2*kChunkSize)
      {
          .addr = 0,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(Oom(ctx.pool));

  // Allocations should now fail.
  {
    auto result = ctx.pool.Allocate(Type::kPoolTestPayload, 1, 1);
    ASSERT_TRUE(result.is_error());
  }

  // Same for frees that subdivide ranges. In this case, we can free one byte
  // from any of the allocated ranges (which were two bytes each).
  {
    auto it = std::find_if(ctx.pool.begin(), ctx.pool.end(), [](const Range& range) {
      return range.type != Type::kPoolBookkeeping && range.type != Type::kFreeRam && range.size > 1;
    });
    ASSERT_NE(ctx.pool.end(), it);
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, it->addr, 1, /*free_error=*/true));
  }

  // Ditto for any weak allocations that result in subdivision.
  {
    auto it = std::find_if(ctx.pool.begin(), ctx.pool.end(), [](const Range& range) {
      return range.type == Type::kFreeRam && range.size > 1;
    });
    ASSERT_NE(ctx.pool.end(), it);
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            it->addr, 1,
                                                            /*alloc_error=*/true));
  }
}

}  // namespace
