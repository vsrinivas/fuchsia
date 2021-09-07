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

using memalloc::MemRange;
using memalloc::Pool;
using memalloc::Type;

constexpr uint64_t kChunkSize = Pool::kBookkeepingChunkSize;
constexpr uint64_t kUsableMemoryStart = Pool::kNullPointerRegionEnd;
constexpr uint64_t kDefaultAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
constexpr uint64_t kDefaultMaxAddr = std::numeric_limits<uintptr_t>::max();
constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

constexpr const char* kPrintOutPrefix = "PREFIX";

constexpr std::string_view kEmptyPrintOut =
    R"""(PREFIX: | Physical memory range                    | Size    | Type
)""";

void TestPoolInit(Pool& pool, cpp20::span<MemRange> input, bool init_error = false) {
  auto status = pool.Init(std::array{input});
  if (init_error) {
    ASSERT_TRUE(status.is_error());
    return;
  }
  ASSERT_FALSE(status.is_error());
}

void TestPoolContents(Pool& pool, cpp20::span<const MemRange> expected) {
  std::vector<const MemRange> actual(pool.begin(), pool.end());
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));
}

void TestPoolPrintOut(Pool& pool, const char* prefix, std::string_view expected) {
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

void TestPoolAllocation(Pool& pool, Type type, uint64_t size, uint64_t alignment, uint64_t max_addr,
                        bool alloc_error = false) {
  auto result = pool.Allocate(type, size, alignment, max_addr);
  if (alloc_error) {
    ASSERT_TRUE(result.is_error());
    return;
  }
  ASSERT_FALSE(result.is_error());

  // The resulting range should now be contained in one of the tracked ranges.
  const MemRange contained = {
      .addr = std::move(result).value(),
      .size = size,
      .type = type,
  };
  auto is_contained = [&pool](const MemRange& subrange) -> bool {
    for (const MemRange& range : pool) {
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
    for (const MemRange& range : pool) {
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

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {}, /*init_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
  ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
}

TEST(MemallocPoolTests, NoRam) {
  PoolContext ctx;
  MemRange ranges[] = {
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

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, /*init_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
  ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
}

TEST(MemallocPoolTests, TooLittleRam) {
  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [0, kChunkSize - 1) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize - 1,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // reserved: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // RAM: [kChunkSize, kChunkSize/2) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // reserved: [kChunkSize/2, 3*kChunkSize/4) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize / 2,
            .size = kChunkSize / 4,
            .type = Type::kReserved,
        },
        // RAM: [3*kChunkSize/4, 7*kChunkSize/8) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 3 * kChunkSize / 4,
            .size = kChunkSize / 8,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }
}

TEST(MemallocPoolTests, NullPointerRegionIsAvoided) {
  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [kUsableMemoryStart - kChunkSize, kUsableMemoryStart)
        {
            .addr = kUsableMemoryStart - kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [0, kUsableMemoryStart)
        {
            .addr = 0,
            .size = kUsableMemoryStart,
            .type = Type::kFreeRam,
        },
    };

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}, /*init_error=*/true));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kEmptyPrintOut));
  }
}

TEST(MemallocPoolTests, Bookkeeping) {
  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000010000, 0x0000000000011000) |      4k | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [kChunkSize, 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000010000, 0x0000000000011000) |      4k | bookkeeping
PREFIX: | [0x0000000000011000, 0x0000000000012000) |      4k | free RAM
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // reserved: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // RAM: [kChunkSize, 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // reserved: [0, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // bookkeeping: [kChunkSize, 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000010000, 0x0000000000011000) |      4k | reserved
PREFIX: | [0x0000000000011000, 0x0000000000012000) |      4k | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [kChunkSize/2, 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize / 2,
            .size = 3 * kChunkSize / 2,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // RAM: [kChunkSize/2, kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize / 2,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // bookkeeping: [kChunkSize, 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000010800, 0x0000000000011000) |      2k | free RAM
PREFIX: | [0x0000000000011000, 0x0000000000012000) |      4k | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // RAM: [0, 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // reserved: [kChunkSize/2, 3*kChunkSize/2) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize / 2,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // RAM: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // RAM: [0. kChunkSize/2) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // reserved: [kChunkSize/2. 3*kChunkSize/2) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize / 2,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // RAM: [3*kChunkSize/2. 2*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 3 * kChunkSize / 2,
            .size = kChunkSize / 2,
            .type = Type::kFreeRam,
        },
        // bookkeeping: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 2 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000010000, 0x0000000000010800) |      2k | free RAM
PREFIX: | [0x0000000000010800, 0x0000000000011800) |      4k | reserved
PREFIX: | [0x0000000000011800, 0x0000000000012000) |      2k | free RAM
PREFIX: | [0x0000000000012000, 0x0000000000013000) |      4k | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }
}

TEST(MemallocPoolTests, NullPointerRegionIsAutoPopulated) {
  {
    PoolContext ctx;
    MemRange ranges[] = {
        // free RAM: [0, kUsableMemoryStart + kChunkSize)
        {
            .addr = 0,
            .size = kUsableMemoryStart + kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // null pointer region: [0, kUsableMemoryStart)
        {
            .addr = 0,
            .size = kUsableMemoryStart,
            .type = Type::kNullPointerRegion,
        },
        // bookkeeping: [kUsableMemoryStart, kUsableMemoryStart + kChunkSize)
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000010000) |     64k | null pointer region
PREFIX: | [0x0000000000010000, 0x0000000000011000) |      4k | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }

  {
    PoolContext ctx;
    MemRange ranges[] = {
        // free RAM: [0, kChunkSize)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        // reserved: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // RAM: [5*kChunkSize, 7*kChunkSize)
        {
            .addr = 5 * kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // peripheral: [9*kChunkSize, 10*kChunkSize)
        {
            .addr = 9 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPeripheral,
        },
        // free RAM: [kUsableMemoryStart - kChunkSize, kUsableMemoryStart + kChunkSize)
        {
            .addr = kUsableMemoryStart - kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
    };

    const MemRange expected[] = {
        // null pointer region: [0, kUsableMemoryStart)
        {
            .addr = 0,
            .size = kChunkSize,
            .type = Type::kNullPointerRegion,
        },
        // reserved: [kChunkSize, 2*kChunkSize)
        {
            .addr = kChunkSize,
            .size = kChunkSize,
            .type = Type::kReserved,
        },
        // null pointer region: [5*kChunkSize, 7*kChunkSize)
        {
            .addr = 5 * kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kNullPointerRegion,
        },
        // peripheral: [9*kChunkSize, 10*kChunkSize)
        {
            .addr = 9 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPeripheral,
        },
        // null pointer region: [kUsableMemoryStart - kChunkSize, kUsableMemoryStart)
        {
            .addr = kUsableMemoryStart - kChunkSize,
            .size = kChunkSize,
            .type = Type::kNullPointerRegion,
        },
        // bookkeeping: [kUsableMemoryStart, kUsableMemoryStart + kChunkSize)
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
    };

    constexpr std::string_view kExpectedPrintOut =
        R"""(PREFIX: | Physical memory range                    | Size    | Type
PREFIX: | [0x0000000000000000, 0x0000000000001000) |      4k | null pointer region
PREFIX: | [0x0000000000001000, 0x0000000000002000) |      4k | reserved
PREFIX: | [0x0000000000005000, 0x0000000000007000) |      8k | null pointer region
PREFIX: | [0x0000000000009000, 0x000000000000a000) |      4k | peripheral
PREFIX: | [0x000000000000f000, 0x0000000000010000) |      4k | null pointer region
PREFIX: | [0x0000000000010000, 0x0000000000011000) |      4k | bookkeeping
)""";

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
    ASSERT_NO_FATAL_FAILURE(TestPoolPrintOut(ctx.pool, kPrintOutPrefix, kExpectedPrintOut));
  }
}

TEST(MemallocPoolTests, GetContainingRange) {
  PoolContext ctx;
  MemRange ranges[] = {
      // RAM: [0, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = 3 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  const MemRange expected[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // RAM: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  EXPECT_EQ(expected[0], *ctx.pool.GetContainingRange(kUsableMemoryStart));
  EXPECT_EQ(expected[0], *ctx.pool.GetContainingRange(kUsableMemoryStart + kChunkSize - 1));
  EXPECT_EQ(expected[1], *ctx.pool.GetContainingRange(kUsableMemoryStart + kChunkSize));
  EXPECT_EQ(expected[1], *ctx.pool.GetContainingRange(kUsableMemoryStart + 2 * kChunkSize));
  EXPECT_EQ(expected[1], *ctx.pool.GetContainingRange(kUsableMemoryStart + 3 * kChunkSize - 1));
  EXPECT_FALSE(ctx.pool.GetContainingRange(kUsableMemoryStart + 3 * kChunkSize));
}

TEST(MemallocPoolTests, NoResourcesAllocation) {
  PoolContext ctx;
  MemRange ranges[] = {
      // free RAM: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const MemRange expected[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  // Requested size is too big:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 2 * kChunkSize,
                                             kDefaultAlignment, kDefaultMaxAddr,
                                             /*alloc_error=*/true));
  // Requested alignment is too big:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kChunkSize << 2, kDefaultMaxAddr,
                                             /*alloc_error=*/true));

  // Requested max address is too small:
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kDefaultAlignment, 3 * kChunkSize - 2,
                                             /*alloc_error=*/true));
  ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                             kDefaultAlignment, 2 * kChunkSize,
                                             /*alloc_error=*/true));

  // Nothing should have changed.
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
}

TEST(MemallocPoolTests, ExhaustiveAllocation) {
  MemRange ranges[] = {
      // free RAM: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const MemRange expected_before[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const MemRange expected_after[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kPoolTestPayload,
      },
  };

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize,
                                               kDefaultAlignment, kDefaultMaxAddr));

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    for (size_t i = 0; i < 2; ++i) {
      ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize / 2,
                                                 kDefaultAlignment, kDefaultMaxAddr));
    }

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    for (size_t i = 0; i < 4; ++i) {
      ASSERT_NO_FATAL_FAILURE(TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, kChunkSize / 4,
                                                 kDefaultAlignment, kDefaultMaxAddr));
    }

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }

  {
    PoolContext ctx;

    ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_before}));

    for (size_t i = 0; i < kChunkSize; ++i) {
      ASSERT_NO_FATAL_FAILURE(
          TestPoolAllocation(ctx.pool, Type::kPoolTestPayload, 1, 1, kDefaultMaxAddr));
    }

    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
  }
}

TEST(MemallocPoolTests, Freeing) {
  PoolContext ctx;
  MemRange ranges[] = {
      // RAM: [0, 2*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
      // data ZBI: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kDataZbi,
      },
  };

  const MemRange expected[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [kChunkSize, 2*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
      // data ZBI: [2*kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 2 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kDataZbi,
      },
  };

  const MemRange expected_after[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

  // A subrange of extended type passed to Init() can be freed.
  ASSERT_NO_FATAL_FAILURE(
      TestPoolFreeing(ctx.pool, kUsableMemoryStart + 2 * kChunkSize, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(
      TestPoolFreeing(ctx.pool, kUsableMemoryStart + 5 * kChunkSize / 2, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  // Double-frees should be no-ops.
  ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, kUsableMemoryStart + kChunkSize, kChunkSize));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  ASSERT_NO_FATAL_FAILURE(
      TestPoolFreeing(ctx.pool, kUsableMemoryStart + kChunkSize, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  ASSERT_NO_FATAL_FAILURE(
      TestPoolFreeing(ctx.pool, kUsableMemoryStart + 3 * kChunkSize / 2, kChunkSize / 2));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));

  ASSERT_NO_FATAL_FAILURE(
      TestPoolFreeing(ctx.pool, kUsableMemoryStart + 2 * kChunkSize, kChunkSize));
  ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected_after}));
}

TEST(MemallocPoolTests, FreedAllocations) {
  PoolContext ctx;
  MemRange ranges[] = {
      // free RAM: [0, 2*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = 2 * kChunkSize,
          .type = Type::kFreeRam,
      },
  };
  const MemRange expected[] = {
      // bookkeeping: [0, kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = kChunkSize,
          .type = Type::kPoolBookkeeping,
      },
      // free RAM: [kChunkSize, 2*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + kChunkSize,
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
  MemRange ranges[] = {
      // RAM: [0, 3*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
          .size = 3 * kChunkSize,
          .type = Type::kFreeRam,
      },
      // data ZBI: [3*kChunkSize, 4*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 3 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kDataZbi,
      },
      // RAM: [4*kChunkSize, 5*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 4 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
      // RAM: [5*kChunkSize, 6*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 5 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kPhysKernel,
      },
      // RAM: [6*kChunkSize, 7*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart + 6 * kChunkSize,
          .size = kChunkSize,
          .type = Type::kFreeRam,
      },
  };

  ASSERT_NO_FATAL_FAILURE(TestPoolInit(ctx.pool, {ranges}));

  {
    const MemRange expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // RAM: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kFreeRam,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // RAM: [4*kChunkSize, 5*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // RAM: [6*kChunkSize, 7*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }

  // Updating can happen across an extended type.
  {
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            kUsableMemoryStart, 3 * kChunkSize));

    const MemRange expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // test payload: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
        // Updated.
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // RAM: [4*kChunkSize, 5*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // RAM: [6*kChunkSize, 7*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));

    // Weak allocation does not affect extended type ranges, even when there
    // is no free RAM in the provided range.
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(
        ctx.pool, Type::kPoolTestPayload, kUsableMemoryStart + 3 * kChunkSize, kChunkSize));
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }

  {
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(
        ctx.pool, Type::kPoolTestPayload, kUsableMemoryStart + 3 * kChunkSize, 3 * kChunkSize));

    const MemRange expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // test payload: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
        // Updated.
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // test payload: [4*kChunkSize, 5*kChunkSize) relative to kUsableMemoryStart
        {
            // Updated.
            .addr = kUsableMemoryStart + 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // RAM: [6*kChunkSize, 7*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kFreeRam,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }

  {
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            kUsableMemoryStart, 7 * kChunkSize));

    const MemRange expected[] = {
        // bookkeeping: [0, kChunkSize)
        {
            .addr = kUsableMemoryStart,
            .size = kChunkSize,
            .type = Type::kPoolBookkeeping,
        },
        // test payload: [kChunkSize, 3*kChunkSize) relative to kUsableMemoryStart
        // Updated.
        {
            .addr = kUsableMemoryStart + kChunkSize,
            .size = 2 * kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // data ZBI: [3*kChunkSize, 4*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 3 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kDataZbi,
        },
        // test payload: [4*kChunkSize, 5*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 4 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
        // RAM: [5*kChunkSize, 6*kChunkSize) relative to kUsableMemoryStart
        {
            .addr = kUsableMemoryStart + 5 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPhysKernel,
        },
        // test payload: [6*kChunkSize, 7*kChunkSize) relative to kUsableMemoryStart
        {
            // Updated.
            .addr = kUsableMemoryStart + 6 * kChunkSize,
            .size = kChunkSize,
            .type = Type::kPoolTestPayload,
        },
    };
    ASSERT_NO_FATAL_FAILURE(TestPoolContents(ctx.pool, {expected}));
  }
}

TEST(MemallocPoolTests, OutOfMemory) {
  PoolContext ctx;
  MemRange ranges[] = {
      // free RAM: [0, 2*kChunkSize) relative to kUsableMemoryStart
      {
          .addr = kUsableMemoryStart,
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
    auto it = std::find_if(ctx.pool.begin(), ctx.pool.end(), [](const MemRange& range) {
      return range.type != Type::kPoolBookkeeping && range.type != Type::kFreeRam && range.size > 1;
    });
    ASSERT_NE(ctx.pool.end(), it);
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeing(ctx.pool, it->addr, 1, /*free_error=*/true));
  }

  // Ditto for any weak allocations that result in subdivision.
  {
    auto it = std::find_if(ctx.pool.begin(), ctx.pool.end(), [](const MemRange& range) {
      return range.type == Type::kFreeRam && range.size > 1;
    });
    ASSERT_NE(ctx.pool.end(), it);
    ASSERT_NO_FATAL_FAILURE(TestPoolFreeRamSubrangeUpdating(ctx.pool, Type::kPoolTestPayload,
                                                            it->addr, 1,
                                                            /*alloc_error=*/true));
  }
}

}  // namespace
