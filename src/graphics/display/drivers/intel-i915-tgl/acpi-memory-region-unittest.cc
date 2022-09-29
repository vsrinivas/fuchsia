// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/acpi-memory-region.h"

#include <lib/stdcompat/span.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

namespace i915_tgl {

namespace {

class AcpiMemoryRegionTest : public ::testing::Test {
 public:
  void SetUp() override {
    const zx_status_t status = zx::vmo::create(kVmoSize, /*options=*/0, &vmo_);
    ASSERT_EQ(status, ZX_OK);
    vmo_unowned_ = vmo_.borrow();
  }

 protected:
  static constexpr int kVmoSize = 16;
  uint8_t region_data_buffer_[kVmoSize] = {};
  const cpp20::span<uint8_t> region_data_ = region_data_buffer_;

  zx::vmo vmo_;
  zx::unowned_vmo vmo_unowned_;
};

TEST_F(AcpiMemoryRegionTest, EmptyConstructor) {
  AcpiMemoryRegion memory_region;
  EXPECT_FALSE(memory_region.vmo_for_testing()->is_valid());
  EXPECT_TRUE(memory_region.is_empty());
  EXPECT_TRUE(memory_region.data().empty());
}

TEST_F(AcpiMemoryRegionTest, TestConstructor) {
  AcpiMemoryRegion memory_region(std::move(vmo_), region_data_);
  EXPECT_EQ(memory_region.vmo_for_testing(), vmo_unowned_);
  EXPECT_EQ(memory_region.data().data(), region_data_.data());
  EXPECT_EQ(memory_region.data().size(), region_data_.size());
  EXPECT_FALSE(memory_region.is_empty());
}

TEST_F(AcpiMemoryRegionTest, MoveConstructorEmptiesRhs) {
  AcpiMemoryRegion rhs(std::move(vmo_), region_data_);
  AcpiMemoryRegion lhs = std::move(rhs);

  EXPECT_FALSE(rhs.vmo_for_testing()->is_valid());
  EXPECT_TRUE(rhs.is_empty());
  EXPECT_TRUE(rhs.data().empty());

  EXPECT_EQ(lhs.vmo_for_testing(), vmo_unowned_);
  EXPECT_EQ(lhs.data().data(), region_data_.data());
  EXPECT_EQ(lhs.data().size(), region_data_.size());
  EXPECT_FALSE(lhs.is_empty());
}

TEST_F(AcpiMemoryRegionTest, MoveAssignmentSwapsRhs) {
  AcpiMemoryRegion rhs(std::move(vmo_), region_data_);

  uint8_t lhs_region_data_buffer[kVmoSize] = {};
  const cpp20::span<uint8_t> lhs_region_data = lhs_region_data_buffer;

  zx::vmo lhs_vmo;
  const zx_status_t status = zx::vmo::create(kVmoSize, /*options=*/0, &lhs_vmo);
  ASSERT_EQ(status, ZX_OK);
  const zx::unowned_vmo lhs_vmo_unowned = lhs_vmo.borrow();

  AcpiMemoryRegion lhs(std::move(lhs_vmo), lhs_region_data);
  lhs = std::move(rhs);

  EXPECT_EQ(rhs.vmo_for_testing(), lhs_vmo_unowned);
  EXPECT_EQ(rhs.data().data(), lhs_region_data.data());
  EXPECT_EQ(rhs.data().size(), lhs_region_data.size());
  EXPECT_FALSE(rhs.is_empty());

  EXPECT_EQ(lhs.vmo_for_testing(), vmo_unowned_);
  EXPECT_EQ(lhs.data().data(), region_data_.data());
  EXPECT_EQ(lhs.data().size(), region_data_.size());
  EXPECT_FALSE(lhs.is_empty());
}

}  // namespace

}  // namespace i915_tgl
