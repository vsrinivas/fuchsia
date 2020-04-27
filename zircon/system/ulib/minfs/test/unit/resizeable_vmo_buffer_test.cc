// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resizeable_vmo_buffer.h"

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace minfs {
namespace {

const uint32_t kBlockSize = 8192;

class Device : public storage::VmoidRegistry {
 public:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* vmoid) override {
    *vmoid = storage::Vmoid(17);
    return ZX_OK;
  }
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) override {
    EXPECT_EQ(17, vmoid.TakeId());
    return ZX_OK;
  }
} device;

TEST(ResizeableVmoBufferTest, Grow) {
  ResizeableVmoBuffer buffer(kBlockSize);
  ASSERT_OK(buffer.Attach("test", &device));
  auto detach = fbl::MakeAutoCall([&]() { EXPECT_OK(buffer.Detach(&device)); });
  ASSERT_OK(buffer.Grow(2));
  EXPECT_EQ(2, buffer.capacity());
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));
  memcpy(buffer.Data(1), buf, kBlockSize);
  ASSERT_OK(buffer.Grow(50));
  // Check that after growing, the data is still there.
  EXPECT_BYTES_EQ(buf, buffer.Data(1), kBlockSize);
  EXPECT_EQ(50, buffer.capacity());
}

TEST(ResizeableVmoBufferTest, Shrink) {
  ResizeableVmoBuffer buffer(kBlockSize);
  ASSERT_OK(buffer.Attach("test", &device));
  auto detach = fbl::MakeAutoCall([&]() { EXPECT_OK(buffer.Detach(&device)); });
  ASSERT_OK(buffer.Grow(5));
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));
  memcpy(buffer.Data(1), buf, kBlockSize);
  ASSERT_OK(buffer.Shrink(2));
  EXPECT_BYTES_EQ(buf, buffer.Data(1), kBlockSize);
  EXPECT_EQ(2, buffer.capacity());
}

TEST(ResizeableVmoBufferTest, Zero) {
  ResizeableVmoBuffer buffer(kBlockSize);
  ASSERT_OK(buffer.Attach("test", &device));
  auto detach = fbl::MakeAutoCall([&]() { EXPECT_OK(buffer.Detach(&device)); });
  ASSERT_OK(buffer.Grow(5));
  memset(buffer.Data(0), 'a', kBlockSize);
  buffer.Zero(13, 21);
  const char* p = reinterpret_cast<const char*>(buffer.Data(0));
  for (unsigned i = 0; i < kBlockSize; ++i) {
    if (i < 13 || i >= 13 + 21) {
      EXPECT_EQ('a', p[i]);
    } else {
      EXPECT_EQ(0, p[i]);
    }
  }
}

}  // namespace
}  // namespace minfs
