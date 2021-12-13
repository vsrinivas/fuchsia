// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/pool-mem-config.h>
#include <lib/memalloc/pool.h>

#include <forward_list>

#include <gtest/gtest.h>

namespace {

constexpr uint64_t kChunkSize = memalloc::Pool::kBookkeepingChunkSize;

// The pool uses a callback to convert "physical" address regions allocated for
// bookkeeping space to accessible pointers.  The callback for testing purposes
// just ignores that "physical" address and allocates a new chunk for the Pool
// code to store its bookkeeping data in.
memalloc::Pool::BookkeepingAddressToPointer ForTestPool() {
  // The list in the (move-only) lambda just keeps the pointers all live for
  // the lifetime of the Pool (which owns the wrapped and returned lambda).
  using Bookkeeping = std::forward_list<std::unique_ptr<std::byte[]>>;
  return [bookkeeping = Bookkeeping()](uint64_t addr, uint64_t size) mutable {
    std::byte* chunk = new std::byte[size];  // Uninitialized space.
    bookkeeping.emplace_front(chunk);
    return chunk;
  };
}

TEST(MemallocPoolMemConfigTests, Empty) {
  memalloc::Pool pool(ForTestPool());

  // Initialize with an empty pool.
  memalloc::PoolMemConfig mem_config(pool);
  EXPECT_TRUE(mem_config.empty());
  EXPECT_EQ(mem_config.begin(), mem_config.end());
}

TEST(MemallocPoolMemConfigTests, Ranges) {
  memalloc::Pool pool(ForTestPool());

  memalloc::Range test_pool_ranges[] = {
      {
          .addr = 0,
          .size = kChunkSize * 1000,
          .type = memalloc::Type::kFreeRam,
      },
      {
          .addr = kChunkSize * 50,
          .size = kChunkSize * 2,
          .type = memalloc::Type::kReserved,
      },
      {
          .addr = kChunkSize * 100,
          .size = kChunkSize * 5,
          .type = memalloc::Type::kPeripheral,
      },
  };
  EXPECT_TRUE(pool.Init(std::array{cpp20::span<memalloc::Range>(test_pool_ranges)}).is_ok());
  auto alloc_result = pool.Allocate(memalloc::Type::kPoolTestPayload, kChunkSize * 100);
  ASSERT_TRUE(alloc_result.is_ok());
  EXPECT_EQ(kChunkSize * 105, alloc_result.value());

  constexpr zbi_mem_range_t kExpectedZbiRanges[] = {
      {
          .paddr = 0,
          .length = kChunkSize * 50,
          .type = ZBI_MEM_RANGE_RAM,
      },
      {
          .paddr = kChunkSize * 52,
          .length = kChunkSize * 48,
          .type = ZBI_MEM_RANGE_RAM,
      },
      {
          .paddr = kChunkSize * 100,
          .length = kChunkSize * 5,
          .type = ZBI_MEM_RANGE_PERIPHERAL,
      },
      {
          .paddr = kChunkSize * 105,
          .length = kChunkSize * 895,
          .type = ZBI_MEM_RANGE_RAM,
      },
  };

  memalloc::PoolMemConfig mem_config(pool);

  EXPECT_EQ(std::size(kExpectedZbiRanges),
            static_cast<size_t>(std::distance(mem_config.begin(), mem_config.end())));

  auto it = mem_config.begin();
  for (const zbi_mem_range_t& expected : kExpectedZbiRanges) {
    ASSERT_NE(it, mem_config.end());
    const zbi_mem_range_t range = *it++;
    EXPECT_EQ(expected.paddr, range.paddr);
    EXPECT_EQ(expected.length, range.length);
    EXPECT_EQ(expected.type, range.type);
  }
  EXPECT_EQ(it, mem_config.end());
}

}  // namespace
