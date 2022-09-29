// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/acpi-memory-region-util.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

namespace i915_tgl {

namespace {

// RoundToPageBoundariesChecked() with post-condition checks.
template <typename T>
std::pair<T, size_t> RoundToPageBoundariesChecked(T region_base, size_t region_size) {
  const std::pair<T, size_t> return_value = RoundToPageBoundaries(region_base, region_size);

  auto [first_page_address, page_region_size] = return_value;
  EXPECT_LE(first_page_address, region_base);
  EXPECT_GE(page_region_size, region_size);

  const T page_size = T{zx_system_get_page_size()};
  EXPECT_EQ(0u, first_page_address % page_size);
  EXPECT_EQ(0u, page_region_size % page_size);

  EXPECT_GE(first_page_address + page_region_size, region_base + region_size);

  return return_value;
}

TEST(RoundToPageBoundariesTest, PageAlignedRegion) {
  const zx_paddr_t page_size = zx_paddr_t{zx_system_get_page_size()};

  const auto [first_page_address, page_region_size] =
      RoundToPageBoundariesChecked(100 * page_size, 5 * page_size);
  EXPECT_EQ(first_page_address, 100 * page_size);
  EXPECT_EQ(page_region_size, 5 * page_size);
}

TEST(RoundToPageBoundariesTest, SmallestPageStraddlingRegion) {
  const zx_paddr_t page_size = zx_paddr_t{zx_system_get_page_size()};

  const auto [first_page_address, page_region_size] =
      RoundToPageBoundariesChecked(100 * page_size - 1, 2);
  EXPECT_EQ(first_page_address, 99 * page_size);
  EXPECT_EQ(page_region_size, 2 * page_size);
}

TEST(RoundToPageBoundariesTest, PageStraddlingRegion) {
  const zx_paddr_t page_size = zx_paddr_t{zx_system_get_page_size()};

  const auto [first_page_address, page_region_size] =
      RoundToPageBoundariesChecked(100 * page_size - 1, 2 + 5 * page_size);
  EXPECT_EQ(first_page_address, 99 * page_size);
  EXPECT_EQ(page_region_size, 7 * page_size);
}

}  // namespace

}  // namespace i915_tgl
