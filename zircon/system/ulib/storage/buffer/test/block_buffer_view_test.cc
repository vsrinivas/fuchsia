// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/block_buffer_view.h"

#include <lib/zx/vmo.h>

#include <array>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <storage/buffer/vmo_buffer.h>

namespace storage {
namespace {

using ::testing::_;

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
const uint32_t kBlockSize = 8192;
constexpr char kGoldenLabel[] = "test-vmo";

class MockVmoidRegistry : public VmoidRegistry {
 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, Vmoid* out) override {
    *out = Vmoid(kGoldenVmoid);
    return ZX_OK;
  }

  zx_status_t BlockDetachVmo(Vmoid vmoid) final {
    EXPECT_EQ(kGoldenVmoid, vmoid.TakeId());
    return ZX_OK;
  }
};

TEST(BlockBufferViewTest, EmptyView) {
  BlockBufferView view;
  EXPECT_EQ(view.start(), 0ul);
  EXPECT_EQ(view.length(), 0ul);
  EXPECT_EQ(BLOCK_VMOID_INVALID, view.vmoid());
  EXPECT_EQ(view.BlockSize(), 0ul);
}

class BlockBufferViewFixture : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);
    memset(buf_a.data(), 'a', buf_a.size());
    memset(buf_b.data(), 'b', buf_b.size());
    memset(buf_c.data(), 'c', buf_c.size());
    memcpy(buffer.Data(0), buf_a.data(), kBlockSize);
    memcpy(buffer.Data(1), buf_b.data(), kBlockSize);
    memcpy(buffer.Data(2), buf_c.data(), kBlockSize);
  }

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  std::array<char, kBlockSize> buf_a;
  std::array<char, kBlockSize> buf_b;
  std::array<char, kBlockSize> buf_c;
};

using BlockBufferViewTestWithFixture = BlockBufferViewFixture;

TEST_F(BlockBufferViewTestWithFixture, WholeView) {
  BlockBufferView view(&buffer, 0, kCapacity);
  EXPECT_EQ(view.start(), 0ul);
  ASSERT_EQ(kCapacity, view.length());
  ASSERT_EQ(kBlockSize, view.BlockSize());
  EXPECT_EQ(memcmp(buf_a.data(), view.Data(0), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_b.data(), view.Data(1), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_c.data(), view.Data(2), kBlockSize), 0);
}

TEST_F(BlockBufferViewTestWithFixture, PartialView) {
  BlockBufferView view(&buffer, 1, 1);
  EXPECT_EQ(view.start(), 1ul);
  EXPECT_EQ(view.length(), 1ul);
  EXPECT_EQ(memcmp(buf_b.data(), view.Data(0), kBlockSize), 0);
}

TEST_F(BlockBufferViewTestWithFixture, WraparoundBeforeEndView) {
  BlockBufferView view(&buffer, 2, kCapacity);
  EXPECT_EQ(view.start(), 2ul);
  ASSERT_EQ(kCapacity, view.length());
  ASSERT_EQ(kBlockSize, view.BlockSize());
  EXPECT_EQ(memcmp(buf_c.data(), view.Data(0), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_a.data(), view.Data(1), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_b.data(), view.Data(2), kBlockSize), 0);
}

TEST_F(BlockBufferViewTestWithFixture, WraparoundAtEndView) {
  BlockBufferView view(&buffer, kCapacity, kCapacity);
  EXPECT_EQ(view.start(), 0ul);
  ASSERT_EQ(kCapacity, view.length());
  ASSERT_EQ(kBlockSize, view.BlockSize());
  EXPECT_EQ(memcmp(buf_a.data(), view.Data(0), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_b.data(), view.Data(1), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_c.data(), view.Data(2), kBlockSize), 0);
}

TEST_F(BlockBufferViewTestWithFixture, CreateSubViewNoOffsetNoWraparound) {
  BlockBufferView view(&buffer, 0, kCapacity);
  const size_t kNewRelativeStart = 0;
  const size_t kNewLength = 1;
  BlockBufferView subview(view.CreateSubView(kNewRelativeStart, kNewLength));
  EXPECT_EQ(kNewRelativeStart, subview.start());
  ASSERT_EQ(kNewLength, subview.length());
  ASSERT_EQ(kBlockSize, subview.BlockSize());
  EXPECT_EQ(memcmp(buf_a.data(), subview.Data(0), kBlockSize), 0);
}

TEST_F(BlockBufferViewTestWithFixture, CreateSubViewWithOffsetNoWraparound) {
  const size_t kOldStart = 1;
  BlockBufferView view(&buffer, kOldStart, kCapacity);
  const size_t kNewRelativeStart = 1;
  const size_t kNewLength = 1;
  BlockBufferView subview(view.CreateSubView(kNewRelativeStart, kNewLength));
  EXPECT_EQ(kOldStart + kNewRelativeStart, subview.start());
  EXPECT_EQ(kNewLength, subview.length());
  ASSERT_EQ(kBlockSize, subview.BlockSize());
  EXPECT_EQ(memcmp(buf_c.data(), subview.Data(0), kBlockSize), 0);
}

TEST_F(BlockBufferViewTestWithFixture, CreateSubViewWithOffsetAndWraparound) {
  const size_t kOldStart = 1;
  BlockBufferView view(&buffer, kOldStart, kCapacity);
  const size_t kNewRelativeStart = 1;
  const size_t kNewLength = 2;
  BlockBufferView subview(view.CreateSubView(kNewRelativeStart, kNewLength));
  EXPECT_EQ(kOldStart + kNewRelativeStart, subview.start());
  ASSERT_EQ(kNewLength, subview.length());
  ASSERT_EQ(kBlockSize, subview.BlockSize());
  EXPECT_EQ(memcmp(buf_c.data(), subview.Data(0), kBlockSize), 0);
  EXPECT_EQ(memcmp(buf_a.data(), subview.Data(1), kBlockSize), 0);
}

using BlockBufferViewDeathTest = BlockBufferViewFixture;

TEST_F(BlockBufferViewDeathTest, CreateTooLongSubViewThrowsAssertion) {
  BlockBufferView view(&buffer, 0, kCapacity);

  view.CreateSubView(0, kCapacity);
  ASSERT_DEATH({ view.CreateSubView(0, kCapacity + 1); }, _);
}

TEST_F(BlockBufferViewDeathTest, CreateTooLongSubViewAtOffsetThrowsAssertion) {
  BlockBufferView view(&buffer, 0, kCapacity);

  view.CreateSubView(1, kCapacity - 1);
  ASSERT_DEATH({ view.CreateSubView(1, kCapacity); }, _);
}

}  // namespace
}  // namespace storage
