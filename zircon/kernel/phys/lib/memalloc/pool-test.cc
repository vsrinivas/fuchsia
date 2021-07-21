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

}  // namespace
