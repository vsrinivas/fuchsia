// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/optional.h>
#include <zircon/limits.h>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/media/lib/codec_impl/include/lib/media/codec_impl/fake_map_range.h"

namespace {

constexpr uint32_t kFakeRangeCount = 3;

// The +2 is because we want to cover the worst case where only the first byte of the buffer
// overlaps the first page and only the last byte of the buffer overlaps the last page.
constexpr size_t kBufferSize = 64 * ZX_PAGE_SIZE + 2;

volatile uint8_t g_volatile_byte;

}  // namespace

class FakeMapRangeTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_EQ(ZX_OK, FakeMapRange::Create(kBufferSize, &fake_ranges_[0]));
    first_vmar_byte_offset_ = 0;
    last_vmar_byte_offset_ = fbl::round_up(ZX_PAGE_SIZE - 1 + kBufferSize, ZX_PAGE_SIZE) - 1;
  }

 protected:
  // We store the ranges this way in the test because this is how codec_impl has them stored.
  cpp17::optional<FakeMapRange> fake_ranges_[kFakeRangeCount];
  uint32_t first_vmar_byte_offset_ = 0;
  uint32_t last_vmar_byte_offset_ = 0;
};

TEST_F(FakeMapRangeTest, ReadFirstByteFaults) {
  ASSERT_DEATH(g_volatile_byte = fake_ranges_[0]->base()[first_vmar_byte_offset_], "");
}

TEST_F(FakeMapRangeTest, ReadLastByteFaults) {
  ASSERT_DEATH(g_volatile_byte = fake_ranges_[0]->base()[last_vmar_byte_offset_], "");
}

TEST_F(FakeMapRangeTest, WriteFirstByteFaults) {
  ASSERT_DEATH(
      static_cast<volatile uint8_t*>(fake_ranges_[0]->base())[first_vmar_byte_offset_] = 42, "");
}

TEST_F(FakeMapRangeTest, WriteLastByteFaults) {
  ASSERT_DEATH(static_cast<volatile uint8_t*>(fake_ranges_[0]->base())[last_vmar_byte_offset_] = 42,
               "");
}

TEST_F(FakeMapRangeTest, SizeWorks) { ASSERT_EQ(kBufferSize, fake_ranges_[0]->size()); }
