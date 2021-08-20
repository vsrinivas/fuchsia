// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/pool-mem-config.h>
#include <lib/boot-shim/test-helper.h>
#include <lib/memalloc/pool.h>

#include <forward_list>
#include <memory>

#include <zxtest/zxtest.h>

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

using TestShim = boot_shim::BootShim<boot_shim::PoolMemConfigItem>;

TEST(BootShimTests, PoolMemConfigItem) {
  boot_shim::testing::TestHelper test;
  TestShim shim("PoolMemConfigItem", test.log());

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);

  auto& item = shim.Get<boot_shim::PoolMemConfigItem>();
  memalloc::Pool pool(ForTestPool());

  // Not initialized yet: no item.
  EXPECT_EQ(0, item.size_bytes());

  // Initialize with an empty pool: still no item.
  item.Init(pool);
  EXPECT_EQ(0, item.size_bytes());

  // Make sure no item means no item.
  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  for (auto [header, payload] : zbi) {
    EXPECT_NE(header->type, ZBI_TYPE_MEM_CONFIG);
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  // Now actually initialize the pool.
  memalloc::MemRange test_pool_ranges[] = {
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
  };
  EXPECT_TRUE(pool.Init(std::array{cpp20::span<memalloc::MemRange>(test_pool_ranges)}).is_ok());
  auto alloc_result = pool.Allocate(memalloc::Type::kPoolTestPayload, kChunkSize * 100);
  ASSERT_TRUE(alloc_result.is_ok());
  EXPECT_EQ(kChunkSize * 52, alloc_result.value());

  constexpr zbi_mem_range_t kExpectedZbiRanges[] = {
      {
          .paddr = 0,
          .length = kChunkSize * 50,
          .type = ZBI_MEM_RANGE_RAM,
      },
      {
          .paddr = kChunkSize * 50,
          .length = kChunkSize * 2,
          .type = ZBI_MEM_RANGE_RESERVED,
      },
      {
          .paddr = kChunkSize * 52,
          .length = kChunkSize * 948,
          .type = ZBI_MEM_RANGE_RAM,
      },
  };

  // The item points to the Pool so it regenerates its results from it on each
  // call without repeating pool.Init(item).
  EXPECT_GT(item.size_bytes(), sizeof(kExpectedZbiRanges));

  EXPECT_TRUE(shim.AppendItems(zbi).is_ok());

  TestShim::DataZbi::payload_type item_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_MEM_CONFIG) {
      EXPECT_TRUE(item_payload.empty());
      item_payload = payload;
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(cpp20::span(kExpectedZbiRanges).size_bytes(), item_payload.size_bytes());

  const cpp20::span mem_config{
      reinterpret_cast<const zbi_mem_range_t*>(item_payload.data()),
      item_payload.size_bytes() / sizeof(zbi_mem_range_t),
  };
  ASSERT_EQ(std::size(kExpectedZbiRanges), std::size(mem_config));
  for (size_t i = 0; i < std::size(kExpectedZbiRanges); ++i) {
    EXPECT_EQ(kExpectedZbiRanges[i].paddr, mem_config[i].paddr);
    EXPECT_EQ(kExpectedZbiRanges[i].length, mem_config[i].length);
    EXPECT_EQ(kExpectedZbiRanges[i].type, mem_config[i].type);
  }
}

}  // namespace
