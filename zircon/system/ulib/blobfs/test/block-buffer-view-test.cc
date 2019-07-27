// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

#include <blobfs/block-buffer-view.h>
#include <blobfs/format.h>
#include <blobfs/vmo-buffer.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
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
  EXPECT_EQ(VMOID_INVALID, view.vmoid());
}

class BlockBufferViewFixture : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
    memset(buf_a, 'a', sizeof(buf_a));
    memset(buf_b, 'b', sizeof(buf_b));
    memset(buf_c, 'c', sizeof(buf_c));
    memcpy(buffer.Data(0), buf_a, kBlobfsBlockSize);
    memcpy(buffer.Data(1), buf_b, kBlobfsBlockSize);
    memcpy(buffer.Data(2), buf_c, kBlobfsBlockSize);
  }

  MockVmoidRegistry registry;
  VmoBuffer buffer;
  char buf_a[kBlobfsBlockSize];
  char buf_b[kBlobfsBlockSize];
  char buf_c[kBlobfsBlockSize];
};

using BlockBufferViewTest = BlockBufferViewFixture;

TEST_F(BlockBufferViewTest, WholeView) {
  BlockBufferView view(&buffer, 0, kCapacity);
  EXPECT_EQ(0, view.start());
  EXPECT_EQ(kCapacity, view.length());
  EXPECT_EQ(0, memcmp(buf_a, view.Data(0), kBlobfsBlockSize));
  EXPECT_EQ(0, memcmp(buf_b, view.Data(1), kBlobfsBlockSize));
  EXPECT_EQ(0, memcmp(buf_c, view.Data(2), kBlobfsBlockSize));
}

TEST_F(BlockBufferViewTest, PartialView) {
  BlockBufferView view(&buffer, 1, 1);
  EXPECT_EQ(1, view.start());
  EXPECT_EQ(1, view.length());
  EXPECT_EQ(0, memcmp(buf_b, view.Data(0), kBlobfsBlockSize));
}

TEST_F(BlockBufferViewTest, WraparoundBeforeEndView) {
  BlockBufferView view(&buffer, 2, kCapacity);
  EXPECT_EQ(2, view.start());
  EXPECT_EQ(kCapacity, view.length());
  EXPECT_EQ(0, memcmp(buf_c, view.Data(0), kBlobfsBlockSize));
  EXPECT_EQ(0, memcmp(buf_a, view.Data(1), kBlobfsBlockSize));
  EXPECT_EQ(0, memcmp(buf_b, view.Data(2), kBlobfsBlockSize));
}

TEST_F(BlockBufferViewTest, WraparoundAtEndView) {
  BlockBufferView view(&buffer, kCapacity, kCapacity);
  EXPECT_EQ(0, view.start());
  EXPECT_EQ(kCapacity, view.length());
  EXPECT_EQ(0, memcmp(buf_a, view.Data(0), kBlobfsBlockSize));
  EXPECT_EQ(0, memcmp(buf_b, view.Data(1), kBlobfsBlockSize));
  EXPECT_EQ(0, memcmp(buf_c, view.Data(2), kBlobfsBlockSize));
}

}  // namespace
}  // namespace blobfs
