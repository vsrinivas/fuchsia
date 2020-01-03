// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/block_buffer_view.h"

#include <lib/zx/vmo.h>

#include <array>

#include <storage/buffer/vmo_buffer.h>
#include <zxtest/zxtest.h>

namespace storage {
namespace {

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
const uint32_t kBlockSize = 8192;
constexpr char kGoldenLabel[] = "test-vmo";

class MockVmoidRegistry : public VmoidRegistry {
 public:
  bool detached() const { return detached_; }

 private:
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) override {
    *out = kGoldenVmoid;
    return ZX_OK;
  }

  zx_status_t DetachVmo(vmoid_t vmoid) final {
    EXPECT_EQ(kGoldenVmoid, vmoid);
    EXPECT_FALSE(detached_);
    detached_ = true;
    return ZX_OK;
  }

  bool detached_ = false;
};

TEST(BlockBufferViewTest, EmptyView) {
  BlockBufferView view;
  EXPECT_EQ(0, view.start());
  EXPECT_EQ(0, view.length());
  EXPECT_EQ(BLOCK_VMOID_INVALID, view.vmoid());
  EXPECT_EQ(0, view.BlockSize());
}

class BlockBufferViewFixture : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));
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

using BlockBufferViewTest = BlockBufferViewFixture;

TEST_F(BlockBufferViewTest, WholeView) {
  BlockBufferView view(&buffer, 0, kCapacity);
  EXPECT_EQ(0, view.start());
  ASSERT_EQ(kCapacity, view.length());
  ASSERT_EQ(kBlockSize, view.BlockSize());
  EXPECT_BYTES_EQ(buf_a.data(), view.Data(0), kBlockSize);
  EXPECT_BYTES_EQ(buf_b.data(), view.Data(1), kBlockSize);
  EXPECT_BYTES_EQ(buf_c.data(), view.Data(2), kBlockSize);
}

TEST_F(BlockBufferViewTest, PartialView) {
  BlockBufferView view(&buffer, 1, 1);
  EXPECT_EQ(1, view.start());
  EXPECT_EQ(1, view.length());
  EXPECT_BYTES_EQ(buf_b.data(), view.Data(0), kBlockSize);
}

TEST_F(BlockBufferViewTest, WraparoundBeforeEndView) {
  BlockBufferView view(&buffer, 2, kCapacity);
  EXPECT_EQ(2, view.start());
  ASSERT_EQ(kCapacity, view.length());
  ASSERT_EQ(kBlockSize, view.BlockSize());
  EXPECT_BYTES_EQ(buf_c.data(), view.Data(0), kBlockSize);
  EXPECT_BYTES_EQ(buf_a.data(), view.Data(1), kBlockSize);
  EXPECT_BYTES_EQ(buf_b.data(), view.Data(2), kBlockSize);
}

TEST_F(BlockBufferViewTest, WraparoundAtEndView) {
  BlockBufferView view(&buffer, kCapacity, kCapacity);
  EXPECT_EQ(0, view.start());
  ASSERT_EQ(kCapacity, view.length());
  ASSERT_EQ(kBlockSize, view.BlockSize());
  EXPECT_BYTES_EQ(buf_a.data(), view.Data(0), kBlockSize);
  EXPECT_BYTES_EQ(buf_b.data(), view.Data(1), kBlockSize);
  EXPECT_BYTES_EQ(buf_c.data(), view.Data(2), kBlockSize);
}

TEST_F(BlockBufferViewTest, CreateSubViewNoOffsetNoWraparound) {
  BlockBufferView view(&buffer, 0, kCapacity);
  const size_t kNewRelativeStart = 0;
  const size_t kNewLength = 1;
  BlockBufferView subview(view.CreateSubView(kNewRelativeStart, kNewLength));
  EXPECT_EQ(kNewRelativeStart, subview.start());
  ASSERT_EQ(kNewLength, subview.length());
  ASSERT_EQ(kBlockSize, subview.BlockSize());
  EXPECT_BYTES_EQ(buf_a.data(), subview.Data(0), kBlockSize);
}

TEST_F(BlockBufferViewTest, CreateSubViewWithOffsetNoWraparound) {
  const size_t kOldStart = 1;
  BlockBufferView view(&buffer, kOldStart, kCapacity);
  const size_t kNewRelativeStart = 1;
  const size_t kNewLength = 1;
  BlockBufferView subview(view.CreateSubView(kNewRelativeStart, kNewLength));
  EXPECT_EQ(kOldStart + kNewRelativeStart, subview.start());
  EXPECT_EQ(kNewLength, subview.length());
  ASSERT_EQ(kBlockSize, subview.BlockSize());
  EXPECT_BYTES_EQ(buf_c.data(), subview.Data(0), kBlockSize);
}

TEST_F(BlockBufferViewTest, CreateSubViewWithOffsetAndWraparound) {
  const size_t kOldStart = 1;
  BlockBufferView view(&buffer, kOldStart, kCapacity);
  const size_t kNewRelativeStart = 1;
  const size_t kNewLength = 2;
  BlockBufferView subview(view.CreateSubView(kNewRelativeStart, kNewLength));
  EXPECT_EQ(kOldStart + kNewRelativeStart, subview.start());
  ASSERT_EQ(kNewLength, subview.length());
  ASSERT_EQ(kBlockSize, subview.BlockSize());
  EXPECT_BYTES_EQ(buf_c.data(), subview.Data(0), kBlockSize);
  EXPECT_BYTES_EQ(buf_a.data(), subview.Data(1), kBlockSize);
}

using BlockBufferViewDeathTest = BlockBufferViewFixture;

TEST_F(BlockBufferViewDeathTest, CreateTooLongSubViewThrowsAssertion) {
  BlockBufferView view(&buffer, 0, kCapacity);

  ASSERT_NO_DEATH([=] { view.CreateSubView(0, kCapacity); });
  ASSERT_DEATH([=] { view.CreateSubView(0, kCapacity + 1); });
}

TEST_F(BlockBufferViewDeathTest, CreateTooLongSubViewAtOffsetThrowsAssertion) {
  BlockBufferView view(&buffer, 0, kCapacity);

  ASSERT_NO_DEATH([=] { view.CreateSubView(1, kCapacity - 1); });
  ASSERT_DEATH([=] { view.CreateSubView(1, kCapacity); });
}

}  // namespace
}  // namespace storage
