// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/items/mem-config.h>
#include <zircon/boot/image.h>

#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(MemRangeMerger, Empty) {
  std::vector<zbi_mem_range_t> input{};
  zbitl::MemRangeMerger merger(input.begin(), input.end());
  EXPECT_EQ(merger.begin(), merger.end());
}

TEST(MemRangeMerger, SingleItem) {
  // Create an input with just a single range.
  std::vector<zbi_mem_range_t> input = {
      zbi_mem_range_t{
          .paddr = 1,
          .length = 2,
          .type = 3,
      },
  };
  zbitl::MemRangeMerger merger(input.begin(), input.end());
  EXPECT_NE(merger.begin(), merger.end());

  // Check first element.
  auto it = merger.begin();
  EXPECT_EQ(it->paddr, 1u);
  EXPECT_EQ(it->length, 2u);
  EXPECT_EQ(it->type, 3u);

  // Should be no more elements.
  ++it;
  EXPECT_EQ(it, merger.end());
}

TEST(MemRangeMerger, MergeItems) {
  // Create an input with multiple ranges to be merged.
  std::vector<zbi_mem_range_t> input = {
      zbi_mem_range_t{
          .paddr = 0,
          .length = 100,
          .type = 1,
      },
      zbi_mem_range_t{
          .paddr = 100,
          .length = 200,
          .type = 1,
      },
      zbi_mem_range_t{
          .paddr = 300,
          .length = 100,
          .type = 1,
      },
  };
  zbitl::MemRangeMerger merger(input.begin(), input.end());

  // Merge the items.
  std::vector<zbi_mem_range_t> result(merger.begin(), merger.end());

  // Ensure we got the correctly merged results.
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].paddr, 0u);
  EXPECT_EQ(result[0].length, 400u);
  EXPECT_EQ(result[0].type, 1u);
}

TEST(MemRangeMerger, ShouldNotCombineNonContiguousItems) {
  // Ensure we don't merge non-contiguous items.
  std::vector<zbi_mem_range_t> input = {
      zbi_mem_range_t{
          .paddr = 0,
          .length = 1,
          .type = 1,
      },
      zbi_mem_range_t{
          .paddr = 2,  // skips byte 1; should not be merged.
          .length = 1,
          .type = 1,
      },
      zbi_mem_range_t{
          .paddr = 3,  // not the same type; should not be merged.
          .length = 1,
          .type = 2,
      },
  };
  zbitl::MemRangeMerger merger(input.begin(), input.end());

  // Merge the items.
  std::vector<zbi_mem_range_t> result(merger.begin(), merger.end());

  // Ensure the input matches the output.
  ASSERT_EQ(result.size(), input.size());
  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i].paddr, result[i].paddr);
    EXPECT_EQ(input[i].length, result[i].length);
    EXPECT_EQ(input[i].type, result[i].type);
  }
}

}  // namespace
