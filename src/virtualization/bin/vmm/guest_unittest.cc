// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

TEST(GuestTest, GuestMemoryPageAligned) {
  const uint32_t page_size = zx_system_get_page_size();
  const uint64_t expected_guest_memory = static_cast<uint64_t>(page_size) * 10;

  // Already page aligned, so no change.
  EXPECT_EQ(expected_guest_memory, Guest::GetPageAlignedGuestMemory(expected_guest_memory));
}

TEST(GuestTest, RoundUpUnalignedGuestMemory) {
  const uint32_t page_size = zx_system_get_page_size();
  const uint64_t expected_guest_memory = static_cast<uint64_t>(page_size) * 10;

  // Memory is unaligned, so this will be rounded up half a page.
  EXPECT_EQ(expected_guest_memory,
            Guest::GetPageAlignedGuestMemory(expected_guest_memory - page_size / 2));
}

TEST(GuestTest, GetSmallGuestMemoryRegion) {
  const uint64_t guest_memory = Guest::GetPageAlignedGuestMemory(0x10000);

  std::vector<GuestMemoryRegion> regions;
  Guest::GenerateGuestMemoryRegions(guest_memory, &regions);

  EXPECT_THAT(regions, ::testing::SizeIs(1));
  EXPECT_THAT(regions[0], ::testing::FieldsAre(0, guest_memory));
}

}  // namespace
